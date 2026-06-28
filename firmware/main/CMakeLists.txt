idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        http_server
        nvs_flash
        storage
        ui
        usb_msc
        version
        webfs
        wifi_mgr
)
