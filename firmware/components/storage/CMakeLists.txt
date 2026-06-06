idf_component_register(
    SRCS
        "sd_raw.c"
        "sd_fatfs.c"
        "sd_owner.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bsp
        esp_driver_sdmmc
        fatfs
        sdmmc
)
