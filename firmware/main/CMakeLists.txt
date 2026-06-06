idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        bsp
        nvs_flash
        storage
        wifi_mgr
)
