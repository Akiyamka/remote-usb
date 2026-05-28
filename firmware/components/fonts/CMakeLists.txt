# `fonts` — the three Maple Mono bitmap fonts plus a flat lookup API
# (`fonts.h`). The font source files live under `firmware/fonts/` and stay
# there so they can be regenerated with `lv_font_conv` independently of any
# component boundary; we pull them in via relative paths.
#
# `priv_include/lvgl.h` is the minimal LVGL ABI shim the generated files
# need. Keeping it in PRIV_INCLUDE_DIRS prevents the shim from leaking out
# of this component, so a future real-LVGL import won't clash.

idf_component_register(
    SRCS
        "fonts.c"
        "lv_font_stubs.c"
        "../../fonts/lv_font_maplemomo_12.c"
        "../../fonts/lv_font_maplemomo_14.c"
        "../../fonts/lv_font_maplemomo_16.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_common
)
