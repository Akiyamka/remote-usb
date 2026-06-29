idf_component_register(
    SRCS
        "ui_state.c"
        "ui_led.c"
        "ui_logo.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp
    PRIV_REQUIRES
        freertos
        version
)
