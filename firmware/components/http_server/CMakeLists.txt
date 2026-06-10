idf_component_register(
    SRCS
        "http_api_status.c"
        "http_server.c"
        "http_static.c"
        "http_util.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_server
        esp_app_format
        storage
        ui
        usb_msc
        webfs
        wifi_mgr
)
