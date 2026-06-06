idf_component_register(
    SRCS
        "usb_msc.c"
        "usb_descriptors.c"
    INCLUDE_DIRS "include"
    REQUIRES
        espressif__esp_tinyusb
        esp_hw_support
        esp_timer
        storage
)
