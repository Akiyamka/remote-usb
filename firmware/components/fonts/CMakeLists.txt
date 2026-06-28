# `fonts` — the pixel bitmap fonts plus a flat lookup API (`fonts.h`).
# The font source files live under `firmware/fonts/` and stay there so they
# can be regenerated independently of any component boundary; we pull them in
# via relative paths.
#
# `priv_include/lvgl.h` is the minimal LVGL ABI shim the generated files
# need. Keeping it in PRIV_INCLUDE_DIRS prevents the shim from leaking out
# of this component, so a future real-LVGL import won't clash.

idf_component_register(
    SRCS
        "fonts.c"
        "lv_font_stubs.c"
        "../../fonts/pixel_font_delicatus_16.c"
        "../../fonts/pixel_font_cairopixel_32.c"
        "../../fonts/pixel_font_quinquefive_5.c"
        "../../fonts/pixel_font_quinquefive_10_digits.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES esp_common
)
