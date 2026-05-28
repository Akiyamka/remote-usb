// SPDX-License-Identifier: MIT
//
// No-op stand-ins for the two LVGL helpers the generated font files take the
// address of. They are stored as callback pointers in `lv_font_t` but the
// project's own renderer never invokes them — it walks `dsc` directly. Both
// stubs trap so any accidental call is loud and obvious instead of silently
// jumping into garbage.

#include "lvgl.h"
#include "esp_system.h"

void lv_font_get_glyph_dsc_fmt_txt(void)
{
    // Unreachable in our usage; abort if something tries to call it.
    abort();
}

void lv_font_get_bitmap_fmt_txt(void)
{
    abort();
}
