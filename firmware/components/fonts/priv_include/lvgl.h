// SPDX-License-Identifier: MIT
//
// Minimal LVGL ABI shim — *NOT* a full LVGL header.
//
// The generated pixel-font files under `firmware/fonts/` include "lvgl.h" and
// initialise `lv_font_t` + supporting structs using designated initialisers.
// We don't want to drag actual LVGL into the firmware just to read those
// tables, so this header declares *only* the types those generated files
// reference, with the same field layout LVGL v9 uses.
//
// Visibility is restricted to `components/fonts/priv_include/`, so this shim
// never leaks out to other components and won't clash with a real LVGL
// install if we ever pull it in later.
//
// Reference: LVGL master, `src/font/lv_font.h` and `lv_font_fmt_txt.h`.

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Version macros ---------------------------------------------------------
// Pretend to be LVGL v9: this picks the right branches in the generated
// font files (const tables, no v8 glyph cache, `fallback` field present).
#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 0
#define LVGL_VERSION_PATCH 0
#define LV_VERSION_CHECK(a, b, c) (0)

// `LV_ATTRIBUTE_LARGE_CONST` — empty in upstream LVGL too on most targets.
#define LV_ATTRIBUTE_LARGE_CONST

// --- Sub-pixel mode enum (referenced in lv_font_t initialiser) -------------
typedef enum {
    LV_FONT_SUBPX_NONE = 0,
    LV_FONT_SUBPX_HOR,
    LV_FONT_SUBPX_VER,
    LV_FONT_SUBPX_BOTH,
} lv_font_subpx_t;

// --- Format-text font tables ------------------------------------------------

typedef enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
} lv_font_fmt_txt_cmap_type_t;

// Per-glyph descriptor. Bit-field widths and order match upstream LVGL so
// that the generated `glyph_dsc` arrays are read back correctly at runtime.
typedef struct {
    uint32_t bitmap_index : 20;  // start byte inside glyph_bitmap[]
    uint32_t adv_w : 12;         // advance width in 1/16 px
    uint8_t box_w;               // glyph box width in px
    uint8_t box_h;               // glyph box height in px
    int8_t ofs_x;                // x offset from caret to glyph box (px)
    int8_t ofs_y;                // y offset baseline → bottom of glyph (px, up = +)
} lv_font_fmt_txt_glyph_dsc_t;

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t *unicode_list;
    const void *glyph_id_ofs_list;
    uint16_t list_length;
    lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const uint8_t *glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t *glyph_dsc;
    const lv_font_fmt_txt_cmap_t *cmaps;
    const void *kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num : 10;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 4;
} lv_font_fmt_txt_dsc_t;

// --- Generic font handle ---------------------------------------------------
//
// We type the callback fields as plain `void(*)(void)`. They get a non-NULL
// value from the generated files (the addresses of two stub functions we
// supply in `lv_font_stubs.c`), but our renderer never calls them — it
// pulls data straight out of `dsc`. Using the most permissive function-
// pointer type avoids signature warnings without enabling actual calls.

typedef void (*lv_font_cb_t)(void);

struct _lv_font;
typedef struct _lv_font lv_font_t;

struct _lv_font {
    lv_font_cb_t get_glyph_dsc;       // unused, kept for ABI parity
    lv_font_cb_t get_glyph_bitmap;    // unused, kept for ABI parity
    int16_t line_height;
    int16_t base_line;                // descent (baseline → bottom of line)
    uint8_t subpx       : 2;
    int8_t underline_position;
    int8_t underline_thickness;
    uint8_t static_bitmap : 1;
    const void *dsc;                  // points at a lv_font_fmt_txt_dsc_t
    const lv_font_t *fallback;        // unused
    void *user_data;                  // unused
};

// Names referenced by the generated file initialisers. The real LVGL exports
// them as functions; we provide signature-compatible no-op stubs that simply
// abort if ever called (see lv_font_stubs.c).
void lv_font_get_glyph_dsc_fmt_txt(void);
void lv_font_get_bitmap_fmt_txt(void);

#ifdef __cplusplus
}
#endif
