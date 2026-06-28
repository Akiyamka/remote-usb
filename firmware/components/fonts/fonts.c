// SPDX-License-Identifier: MIT
//
// Bitmap-font lookup over the generated pixel-font variants.
//
// Internally we know that the generated files expose their data as `lv_font_t`
// with a `lv_font_fmt_txt_dsc_t` payload (FORMAT0_TINY cmaps, 1 bpp glyphs).
// We bend that into the flat `font_glyph_t` shape `fonts.h` advertises.

#include "fonts.h"
#include "lvgl.h"

// Generated tables — declared by the compiled-in font sources.
extern const lv_font_t lv_font_delicatus_16;
extern const lv_font_t lv_font_cairopixel_32;
extern const lv_font_t lv_font_quinquefive_5;
extern const lv_font_t lv_font_quinquefive_10_digits;

static const lv_font_t *font_handle(font_size_t size)
{
    switch (size) {
        case FONT_DELICATUS_16:   return &lv_font_delicatus_16;
        case FONT_CAIROPIXEL_32:  return &lv_font_cairopixel_32;
        case FONT_QUINQUEFIVE_5:  return &lv_font_quinquefive_5;
        case FONT_QUINQUEFIVE_10_DIGITS:
                                  return &lv_font_quinquefive_10_digits;
        default:                  return NULL;
    }
}

uint8_t fonts_grid_px(font_size_t size)
{
    switch (size) {
        case FONT_QUINQUEFIVE_10_DIGITS:
        case FONT_CAIROPIXEL_32: return 2;
        case FONT_DELICATUS_16:
        case FONT_QUINQUEFIVE_5:
        default:                 return 1;
    }
}

// Resolve the glyph id (index into glyph_dsc[]) for a code-point. Returns 0
// for "no glyph".
static uint32_t resolve_glyph_id(const lv_font_fmt_txt_dsc_t *dsc, uint32_t cp)
{
    if (dsc->cmap_num == 0 || dsc->cmaps == NULL) {
        return 0;
    }

    for (uint16_t i = 0; i < dsc->cmap_num; ++i) {
        const lv_font_fmt_txt_cmap_t *cmap = &dsc->cmaps[i];
        if (cmap->type != LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY) {
            continue;
        }
        if (cp < cmap->range_start ||
            cp >= (cmap->range_start + cmap->range_length)) {
            continue;
        }
        return (uint32_t)cmap->glyph_id_start + (cp - cmap->range_start);
    }
    return 0;
}

bool fonts_glyph(font_size_t size, uint32_t unicode, font_glyph_t *out)
{
    if (out == NULL) {
        return false;
    }
    const lv_font_t *font = font_handle(size);
    if (font == NULL || font->dsc == NULL) {
        return false;
    }
    const lv_font_fmt_txt_dsc_t *dsc = (const lv_font_fmt_txt_dsc_t *)font->dsc;
    const uint32_t gid = resolve_glyph_id(dsc, unicode);
    if (gid == 0) {
        // Treat as zero-width "missing": still let caller advance the cursor
        // by the font's typical width (best effort: pretend space-like cell).
        out->bitmap = NULL;
        out->box_w = 0;
        out->box_h = 0;
        out->ofs_x = 0;
        out->ofs_y = 0;
        out->adv_px = 0;
        out->bpp = (uint8_t)dsc->bpp;
        return false;
    }
    const lv_font_fmt_txt_glyph_dsc_t *gd = &dsc->glyph_dsc[gid];
    out->bitmap = dsc->glyph_bitmap + gd->bitmap_index;
    out->box_w = gd->box_w;
    out->box_h = gd->box_h;
    out->ofs_x = gd->ofs_x;
    out->ofs_y = gd->ofs_y;
    // adv_w is stored in 1/16 px; round to nearest integer pixel.
    out->adv_px = (int16_t)(((uint32_t)gd->adv_w + 8u) >> 4);
    out->bpp = (uint8_t)dsc->bpp;
    return true;
}

int16_t fonts_line_height(font_size_t size)
{
    const lv_font_t *font = font_handle(size);
    return (font != NULL) ? font->line_height : 0;
}

int16_t fonts_base_line(font_size_t size)
{
    const lv_font_t *font = font_handle(size);
    return (font != NULL) ? font->base_line : 0;
}
