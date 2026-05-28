// SPDX-License-Identifier: MIT
//
// Public bitmap-font API for the project.
//
// The three Maple Mono variants (12 / 14 / 16 px) live in `firmware/fonts/`
// as auto-generated LVGL tables. This header gives the rest of the firmware
// a *flat* view of them: callers ask for a glyph descriptor + a pointer to
// its 4 bpp bitmap, then read alpha samples with the inline helper below.
// The actual `lv_font_t` struct layout stays private to this component.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FONT_SMALL  = 0,   // Maple Mono 12 px
    FONT_MEDIUM = 1,   // Maple Mono 14 px
    FONT_LARGE  = 2,   // Maple Mono 16 px
    FONT_COUNT,
} font_size_t;

// Resolved glyph view returned by `fonts_glyph()`. All offsets are in pixels.
typedef struct {
    const uint8_t *bitmap;  // base of this glyph's pixel rows in font ROM
    uint16_t box_w;         // visible width in px
    uint16_t box_h;         // visible height in px
    int8_t   ofs_x;         // x offset from caret to glyph box
    int8_t   ofs_y;         // y offset baseline → bottom of glyph box (up = +)
    int16_t  adv_px;        // advance width in whole px (rounded)
    uint8_t  bpp;           // bits per pixel (always 4 for our fonts)
} font_glyph_t;

// Look up a glyph by Unicode code-point. Returns false if the font is invalid,
// the code-point is outside the supported range, or the glyph has no pixels
// (e.g. SPACE — caller should still honour `adv_px`).
bool fonts_glyph(font_size_t size, uint32_t unicode, font_glyph_t *out);

// Vertical metrics for the chosen font.
int16_t fonts_line_height(font_size_t size);  // total line height in px
int16_t fonts_base_line(font_size_t size);    // descent (baseline → bottom)

// Read a 4-bpp alpha sample (0..15) at (x, y) inside a glyph's pixel box.
// Inline-able and bounds-unchecked — caller guarantees 0 ≤ x < box_w and
// 0 ≤ y < box_h.
//
// `lv_font_conv --bpp 4` packs pixels *continuously at the bit level*: rows
// are NOT padded to a whole byte, so a 9-wide glyph uses 4.5 bytes per row
// and the second row starts mid-byte. Indexing by `y * row_bytes + x/2`
// only works when `box_w` is even — for odd widths it shifts every other
// row by half a byte and produces visually corrupted glyphs (notably 'W',
// 'm', '$', and similar wide letters). Treat the bitmap as a flat array of
// 4-bit samples instead. The high nibble of each byte holds the pixel at
// the lower linear index.
static inline uint8_t fonts_pixel_a4(const uint8_t *bitmap,
                                     uint16_t box_w,
                                     uint16_t x, uint16_t y)
{
    const uint32_t idx = (uint32_t)y * box_w + x;
    const uint8_t byte = bitmap[idx >> 1];
    return (idx & 1u) ? (uint8_t)(byte & 0x0Fu)
                      : (uint8_t)((byte >> 4) & 0x0Fu);
}

#ifdef __cplusplus
}
#endif
