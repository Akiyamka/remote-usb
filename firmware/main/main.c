#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "bsp_lcd.h"
#include "bsp_led.h"
#include "sd_fatfs.h"
#include "sd_owner.h"
#include "sd_raw.h"

static const char *TAG = "main";

// Firmware revision banner shown on the welcome screen. Bumped alongside
// each spec/plan revision so we can verify on the LCD which build is
// actually flashed onto the device.
#define APP_VERSION_STR  "v0.0.4"

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

static esp_err_t ensure_phase3_test_file(time_t *mtime)
{
    char path[64];
    snprintf(path, sizeof(path), "%s/test.txt", sd_fatfs_mount_point());

    struct stat st;
    if (stat(path, &st) == 0) {
        *mtime = st.st_mtime;
        return ESP_OK;
    }

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "failed to create %s", path);
        return ESP_FAIL;
    }

    fputs("phase3 sd_owner smoke test\n", file);
    if (fclose(file) != 0) {
        ESP_LOGE(TAG, "failed to close %s", path);
        return ESP_FAIL;
    }

    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "failed to stat %s after create", path);
        return ESP_FAIL;
    }

    *mtime = st.st_mtime;
    return ESP_OK;
}

static esp_err_t phase3_sd_owner_smoke_test(void)
{
    ESP_LOGI(TAG, "Phase 3 sd_owner smoke test: FATFS -> MSC -> FATFS");
    ESP_RETURN_ON_ERROR(sd_owner_init(), TAG, "sd_owner_init");
    ESP_RETURN_ON_ERROR(sd_owner_switch_to_fatfs(), TAG, "switch to FATFS");

    const uint64_t total_mb = sd_fatfs_get_total_bytes() / (1024ULL * 1024ULL);
    const uint64_t free_mb = sd_fatfs_get_free_bytes() / (1024ULL * 1024ULL);
    ESP_LOGI(TAG, "FATFS %s: total=%" PRIu64 " MB free=%" PRIu64 " MB",
             sd_fatfs_mount_point(), total_mb, free_mb);

    time_t initial_mtime = 0;
    ESP_RETURN_ON_ERROR(ensure_phase3_test_file(&initial_mtime), TAG,
                        "ensure test.txt");

    char path[64];
    snprintf(path, sizeof(path), "%s/test.txt", sd_fatfs_mount_point());

    for (uint32_t i = 0; i < 10; ++i) {
        ESP_LOGI(TAG, "switch cycle %" PRIu32 "/10: FATFS -> MSC", i + 1);
        ESP_RETURN_ON_ERROR(sd_owner_switch_to_msc(), TAG, "switch to MSC");
        ESP_LOGI(TAG, "MSC raw media: sectors=%" PRIu32 " sector_size=%" PRIu16,
                 sd_raw_get_sector_count(), sd_raw_get_sector_size());

        ESP_LOGI(TAG, "switch cycle %" PRIu32 "/10: MSC -> FATFS", i + 1);
        ESP_RETURN_ON_ERROR(sd_owner_switch_to_fatfs(), TAG, "switch to FATFS");

        struct stat st;
        if (stat(path, &st) != 0) {
            ESP_LOGE(TAG, "failed to stat %s after cycle %" PRIu32, path, i + 1);
            return ESP_FAIL;
        }
        if (st.st_mtime != initial_mtime) {
            ESP_LOGE(TAG, "%s mtime changed: before=%lld after=%lld",
                     path, (long long)initial_mtime, (long long)st.st_mtime);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Phase 3 sd_owner smoke test done; owner=%s",
             sd_owner_name(sd_owner_current()));
    return ESP_OK;
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

    esp_err_t ret = phase3_sd_owner_smoke_test();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Phase 3 bring-up done; idling");
    } else {
        ESP_LOGE(TAG, "Phase 3 bring-up failed: %s", esp_err_to_name(ret));
    }

    // Heartbeat log so a serial monitor still confirms liveness even when
    // the screen is steady. Will be replaced by the real state machine in
    // later phases.
    uint32_t tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "alive tick=%" PRIu32, ++tick);
    }
}
