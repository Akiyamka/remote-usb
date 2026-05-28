#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "bsp_lcd.h"
#include "bsp_led.h"

static const char *TAG = "main";

// Firmware revision banner shown on the welcome screen. Bumped alongside
// each spec/plan revision so we can verify on the LCD which build is
// actually flashed onto the device.
#define APP_VERSION_STR  "v0.0.3"

static void phase1_welcome_screen(void)
{
    // Layout — landscape 160x80, Maple Mono Large (line_height = 19 px).
    // Two lines stacked, vertically centred with a small inter-line gap.
    const int16_t line_h   = 19;
    const int16_t gap      = 2;
    const int16_t total_h  = (int16_t)(line_h * 2 + gap);
    const int16_t y_top    = (int16_t)((BSP_LCD_HEIGHT - total_h) / 2);
    const int16_t y_bottom = (int16_t)(y_top + line_h + gap);

    ESP_ERROR_CHECK(bsp_lcd_clear(BSP_LCD_BLACK));
    ESP_ERROR_CHECK(bsp_lcd_draw_text_centered(y_top, FONT_LARGE,
                                               "Welcome",
                                               BSP_LCD_WHITE, BSP_LCD_BLACK));
    ESP_ERROR_CHECK(bsp_lcd_draw_text_centered(y_bottom, FONT_LARGE,
                                               APP_VERSION_STR,
                                               BSP_LCD_GREEN, BSP_LCD_BLACK));
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot " APP_VERSION_STR);

    // Phase 1 BSP bring-up: LED + LCD.
    ESP_ERROR_CHECK(bsp_led_init());
    ESP_ERROR_CHECK(bsp_lcd_init());

    phase1_welcome_screen();

    // Spec §12.1 calls for "LED solid green" briefly after a successful
    // boot. Phase 1's deliverable is exactly that — hold it for 2 seconds
    // so anyone glancing at the dongle can see firmware is alive.
    ESP_ERROR_CHECK(bsp_led_set_rgb(0, 255, 0));
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_ERROR_CHECK(bsp_led_off());

    ESP_LOGI(TAG, "Phase 1 bring-up done; idling");

    // Heartbeat log so a serial monitor still confirms liveness even when
    // the screen is steady. Will be replaced by the real state machine in
    // later phases.
    uint32_t tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive tick=%" PRIu32, ++tick);
    }
}
