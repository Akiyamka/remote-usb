#!/usr/bin/env python3
"""Generate 1-bpp LVGL-shaped font tables from the project pixel fonts."""

from __future__ import annotations

from dataclasses import dataclass
from math import gcd
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


FIRST_CP = 32
LAST_CP_EXCLUSIVE = 127
CANVAS_SIZE = 320
ORIGIN = CANVAS_SIZE // 2
CUSTOM_DOT_SIZE = 2
CUSTOM_DOT_OFS_X = 1
CUSTOM_DOT_ADV_PX = 6


@dataclass(frozen=True)
class FontConfig:
    source: str
    size_px: int
    symbol: str
    output: str
    grid_px: int
    codepoints: tuple[int, ...] = tuple(range(FIRST_CP, LAST_CP_EXCLUSIVE))
    custom_dot: bool = False


@dataclass(frozen=True)
class Glyph:
    bitmap_index: int
    adv_px: int
    box_w: int
    box_h: int
    ofs_x: int
    ofs_y: int


FONTS = (
    FontConfig("fonts/Delicatus.ttf", 16, "lv_font_delicatus_16",
               "pixel_font_delicatus_16.c", 1),
    FontConfig("fonts/Cairopixel.otf", 32, "lv_font_cairopixel_32",
               "pixel_font_cairopixel_32.c", 2),
    FontConfig("fonts/QuinqueFive.otf", 5, "lv_font_quinquefive_5",
               "pixel_font_quinquefive_5.c", 1),
    FontConfig("fonts/QuinqueFive.otf", 10, "lv_font_quinquefive_10_digits",
               "pixel_font_quinquefive_10_digits.c", 2,
               (ord("."), *range(ord("0"), ord("9") + 1)), True),
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def render_mask(font: ImageFont.FreeTypeFont, ch: str) -> tuple[Image.Image, tuple[int, int, int, int] | None]:
    image = Image.new("L", (CANVAS_SIZE, CANVAS_SIZE), 0)
    draw = ImageDraw.Draw(image)
    draw.fontmode = "1"
    draw.text((ORIGIN, ORIGIN), ch, fill=255, font=font, anchor="ls")
    bbox = image.getbbox()
    if bbox is None:
        return image, None
    return image, (bbox[0] - ORIGIN, bbox[1] - ORIGIN,
                   bbox[2] - ORIGIN, bbox[3] - ORIGIN)


def render_custom_dot() -> tuple[Image.Image, tuple[int, int, int, int]]:
    image = Image.new("L", (CANVAS_SIZE, CANVAS_SIZE), 0)
    for y in range(CUSTOM_DOT_SIZE):
        for x in range(CUSTOM_DOT_SIZE):
            image.putpixel((ORIGIN + CUSTOM_DOT_OFS_X + x, ORIGIN + y), 255)
    return image, (CUSTOM_DOT_OFS_X, 0,
                   CUSTOM_DOT_OFS_X + CUSTOM_DOT_SIZE, CUSTOM_DOT_SIZE)


def pack_1bpp(mask: Image.Image, rel_bbox: tuple[int, int, int, int]) -> list[int]:
    left, top, right, bottom = rel_bbox
    bits: list[int] = []
    current = 0
    bit_count = 0

    for y in range(top, bottom):
        for x in range(left, right):
            current = (current << 1) | (1 if mask.getpixel((ORIGIN + x, ORIGIN + y)) else 0)
            bit_count += 1
            if bit_count == 8:
                bits.append(current)
                current = 0
                bit_count = 0

    if bit_count:
        bits.append(current << (8 - bit_count))

    return bits


def gcd_all(values: list[int]) -> int:
    result = 0
    for value in values:
        result = gcd(result, abs(value))
    return result


def c_array(name: str, data: list[int]) -> str:
    if not data:
        return f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}[] = {{ 0x00 }};\n"

    lines = [f"static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}[] = {{"]
    for offset in range(0, len(data), 12):
        chunk = data[offset:offset + 12]
        rendered = ", ".join(f"0x{byte:02x}" for byte in chunk)
        suffix = "," if offset + 12 < len(data) else ""
        lines.append(f"    {rendered}{suffix}")
    lines.append("};")
    return "\n".join(lines) + "\n"


def cmap_runs(codepoints: tuple[int, ...]) -> list[tuple[int, int, int]]:
    runs: list[tuple[int, int, int]] = []
    glyph_id = 1
    run_start = codepoints[0]
    run_len = 1

    for prev, cp in zip(codepoints, codepoints[1:]):
        if cp == prev + 1:
            run_len += 1
            continue
        runs.append((run_start, run_len, glyph_id))
        glyph_id += run_len
        run_start = cp
        run_len = 1

    runs.append((run_start, run_len, glyph_id))
    return runs


def generate_font(root: Path, cfg: FontConfig) -> tuple[str, dict[str, int]]:
    font = ImageFont.truetype(str(root / cfg.source), cfg.size_px)

    masks: dict[int, tuple[Image.Image, tuple[int, int, int, int] | None]] = {}
    min_y = 0
    max_y = 0
    bounds_values: list[int] = []
    widths: list[int] = []
    heights: list[int] = []
    advs: list[int] = []

    for cp in cfg.codepoints:
        if cfg.custom_dot and cp == ord("."):
            mask, bbox = render_custom_dot()
            adv = CUSTOM_DOT_ADV_PX
        else:
            mask, bbox = render_mask(font, chr(cp))
            adv = int(round(font.getlength(chr(cp))))
        masks[cp] = (mask, bbox)
        advs.append(adv)
        if bbox is None:
            continue
        left, top, right, bottom = bbox
        min_y = min(min_y, top)
        max_y = max(max_y, bottom)
        bounds_values.extend((left, top, right, bottom))
        widths.append(right - left)
        heights.append(bottom - top)

    line_height = max_y - min_y
    base_line = max_y

    bitmap: list[int] = []
    glyphs: list[Glyph] = [
        Glyph(0, 0, 0, 0, 0, 0),
    ]

    for cp in cfg.codepoints:
        mask, bbox = masks[cp]
        if cfg.custom_dot and cp == ord("."):
            adv = CUSTOM_DOT_ADV_PX
        else:
            adv = int(round(font.getlength(chr(cp))))
        bitmap_index = len(bitmap)
        if bbox is None:
            glyphs.append(Glyph(bitmap_index, adv, 0, 0, 0, 0))
            continue

        left, top, right, bottom = bbox
        packed = pack_1bpp(mask, bbox)
        bitmap.extend(packed)
        glyphs.append(Glyph(bitmap_index, adv, right - left, bottom - top,
                            left, -bottom))

    metrics = {
        "line_height": line_height,
        "base_line": base_line,
        "max_advance": max(advs),
        "grid_px": cfg.grid_px,
        "grid_gcd": gcd_all(bounds_values + widths + heights + advs),
        "bitmap_bytes": len(bitmap),
    }

    glyph_lines = [
        "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {",
        "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},",
    ]
    for glyph in glyphs[1:]:
        glyph_lines.append(
            "    {"
            f".bitmap_index = {glyph.bitmap_index}, "
            f".adv_w = {glyph.adv_px * 16}, "
            f".box_w = {glyph.box_w}, "
            f".box_h = {glyph.box_h}, "
            f".ofs_x = {glyph.ofs_x}, "
            f".ofs_y = {glyph.ofs_y}"
            "},"
        )
    glyph_lines.append("};")

    cmap_lines = ["static const lv_font_fmt_txt_cmap_t cmaps[] = {"]
    for start, length, glyph_start in cmap_runs(cfg.codepoints):
        cmap_lines.extend([
            "    {",
            f"        .range_start = {start},",
            f"        .range_length = {length},",
            f"        .glyph_id_start = {glyph_start},",
            "        .unicode_list = NULL,",
            "        .glyph_id_ofs_list = NULL,",
            "        .list_length = 0,",
            "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,",
            "    },",
        ])
    cmap_lines.append("};")
    glyph_note = "ASCII 32..126"
    if cfg.codepoints != tuple(range(FIRST_CP, LAST_CP_EXCLUSIVE)):
        glyph_note = "custom codepoint set"
    if cfg.custom_dot:
        glyph_note += ", handcrafted 2x2 '.', 3px digit gaps"

    return f"""\
/*******************************************************************************
 * Generated by firmware/fonts/generate_pixel_fonts.py
 * Source: {cfg.source}
 * Size: {cfg.size_px} px
 * Bpp: 1
 * Notes: Pillow fontmode=1, no antialiasing, {glyph_note}
 ******************************************************************************/

#include "lvgl.h"

{c_array("glyph_bitmap", bitmap)}
{"\n".join(glyph_lines)}

{"\n".join(cmap_lines)}

static const lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = {len(cmap_runs(cfg.codepoints))},
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
}};

const lv_font_t {cfg.symbol} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height},
    .base_line = {base_line},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .static_bitmap = 0,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
}};
""", metrics


def main() -> None:
    root = repo_root()
    out_dir = root / "firmware" / "fonts"
    for cfg in FONTS:
        source, metrics = generate_font(root, cfg)
        output = out_dir / cfg.output
        output.write_text(source, encoding="utf-8")
        print(
            f"{cfg.output}: line={metrics['line_height']} "
            f"base={metrics['base_line']} adv_max={metrics['max_advance']} "
            f"grid={metrics['grid_px']} shape_gcd={metrics['grid_gcd']} "
            f"bytes={metrics['bitmap_bytes']}"
        )


if __name__ == "__main__":
    main()
