# Board support package — pins, APA102 LED, ST7735 LCD with text renderer.
# `esp_lcd_st7735.c` is the vendor-specific panel driver lifted from the
# upstream wi-fi-drive reference; it slots into IDF's `esp_lcd` HAL.

idf_component_register(
    SRCS
        "bsp_led.c"
        "bsp_lcd.c"
        "esp_lcd_st7735.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_lcd
        fonts
)
