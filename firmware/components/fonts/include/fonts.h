// SPDX-License-Identifier: MIT
//
// Public bitmap-font API for the project.
//
// The pixel-font variants live in `firmware/fonts/` as auto-generated
// LVGL-shaped tables. This header gives the rest of the firmware a *flat*
// view of them: callers ask for a glyph descriptor + a pointer to its bitmap,
// then read binary pixels with the inline helper below.
// The actual `lv_font_t` struct layout stays private to this component.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FONT_DELICATUS_16 = 0,
    FONT_CAIROPIXEL_32,
    FONT_QUINQUEFIVE_5,
    FONT_QUINQUEFIVE_10_DIGITS,
    FONT_COUNT,

    // Compatibility aliases for older call sites.
    FONT_SMALL  = FONT_QUINQUEFIVE_5,
    FONT_MEDIUM = FONT_DELICATUS_16,
    FONT_LARGE  = FONT_CAIROPIXEL_32,
} font_size_t;

// Resolved glyph view returned by `fonts_glyph()`. All offsets are in pixels.
typedef struct {
    const uint8_t *bitmap;  // base of this glyph's pixel rows in font ROM
    uint16_t box_w;         // visible width in px
    uint16_t box_h;         // visible height in px
    int8_t   ofs_x;         // x offset from caret to glyph box
    int8_t   ofs_y;         // y offset baseline → bottom of glyph box (up = +)
    int16_t  adv_px;        // advance width in whole px (rounded)
    uint8_t  bpp;           // bits per pixel (1 for the pixel fonts)
} font_glyph_t;

// Look up a glyph by Unicode code-point. Returns false if the font is invalid,
// the code-point is outside the supported range, or the glyph has no pixels
// (e.g. SPACE — caller should still honour `adv_px`).
bool fonts_glyph(font_size_t size, uint32_t unicode, font_glyph_t *out);

// Vertical metrics for the chosen font.
int16_t fonts_line_height(font_size_t size);  // total line height in px
int16_t fonts_base_line(font_size_t size);    // descent (baseline → bottom)
uint8_t fonts_grid_px(font_size_t size);      // pixel grid step for alignment

// Read a 4-bpp alpha sample (0..15) at (x, y) inside a glyph's pixel box.
// Kept for compatibility with older 4-bpp tables; current pixel-font tables
// use 1 bpp and should be consumed through `fonts_pixel_on()`.
static inline uint8_t fonts_pixel_a4(const uint8_t *bitmap,
                                     uint16_t box_w,
                                     uint16_t x, uint16_t y)
{
    const uint32_t idx = (uint32_t)y * box_w + x;
    const uint8_t byte = bitmap[idx >> 1];
    return (idx & 1u) ? (uint8_t)(byte & 0x0Fu)
                      : (uint8_t)((byte >> 4) & 0x0Fu);
}

// Read a binary pixel at (x, y) inside a glyph's pixel box. Rows are packed
// continuously, with no per-row byte padding. For 1 bpp the most significant
// bit of each byte holds the lower linear pixel index.
static inline bool fonts_pixel_on(const uint8_t *bitmap,
                                  uint16_t box_w,
                                  uint8_t bpp,
                                  uint16_t x, uint16_t y)
{
    if (bitmap == NULL) {
        return false;
    }

    const uint32_t idx = (uint32_t)y * box_w + x;
    if (bpp == 1) {
        const uint8_t byte = bitmap[idx >> 3];
        return (byte & (uint8_t)(0x80u >> (idx & 7u))) != 0;
    }
    if (bpp == 4) {
        return fonts_pixel_a4(bitmap, box_w, x, y) >= 8;
    }
    return false;
}

#ifdef __cplusplus
}
#endif
