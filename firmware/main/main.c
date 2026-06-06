#include <inttypes.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "bsp_lcd.h"
#include "bsp_led.h"
#include "sd_fatfs.h"
#include "sd_owner.h"
#include "wifi_cfg.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

// Firmware revision banner shown on the welcome screen. Bumped alongside
// each spec/plan revision so we can verify on the LCD which build is
// actually flashed onto the device.
#define APP_VERSION_STR  "v0.0.6"

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

static esp_err_t init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t phase5_wifi_smoke_test(void)
{
    ESP_LOGI(TAG, "Phase 5 Wi-Fi smoke test: FATFS wifi.cfg/NVS -> STA");

    esp_err_t ret = init_nvs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sd_owner_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sd_owner_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = sd_owner_switch_to_fatfs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sd_owner_switch_to_fatfs failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "FATFS mounted: total=%" PRIu64 " free=%" PRIu64,
             sd_fatfs_get_total_bytes(), sd_fatfs_get_free_bytes());

    wifi_creds_t creds = {0};
    wifi_creds_source_t source = WIFI_CREDS_NONE;

    ret = wifi_cfg_read_from_sd(&creds);
    if (ret == ESP_OK) {
        source = WIFI_CREDS_FROM_FILE;
        ESP_LOGI(TAG, "Using credentials from /sdcard/wifi.cfg");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ret = wifi_cfg_read_from_nvs(&creds);
        if (ret == ESP_OK) {
            source = WIFI_CREDS_FROM_NVS;
            ESP_LOGI(TAG, "Using credentials from NVS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ret = wifi_cfg_create_default();
            if (ret == ESP_OK) {
                ESP_LOGW(TAG, "Created /sdcard/wifi.cfg; fill it and reboot");
                return ESP_ERR_NOT_FOUND;
            }
            ESP_LOGE(TAG, "Failed to create default wifi.cfg: %s",
                     esp_err_to_name(ret));
            return ret;
        } else {
            ESP_LOGE(TAG, "wifi_cfg_read_from_nvs failed: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "/sdcard/wifi.cfg is invalid: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = wifi_mgr_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_mgr_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = wifi_mgr_connect(&creds, 15000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_mgr_connect failed: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_status_t status = {0};
    wifi_mgr_get_status(&status);
    ESP_LOGI(TAG, "Wi-Fi connected: ssid='%s' ip=%s rssi=%d",
             status.ssid, status.ip_str, status.rssi);

    if (source == WIFI_CREDS_FROM_FILE) {
        ret = wifi_cfg_save_to_nvs(&creds);
        if (ret == ESP_OK) {
            ret = wifi_cfg_delete_from_sd();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to delete wifi.cfg after NVS save: %s",
                         esp_err_to_name(ret));
            }
        } else {
            ESP_LOGW(TAG, "Failed to save credentials to NVS: %s",
                     esp_err_to_name(ret));
        }
    }

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

    esp_err_t ret = phase5_wifi_smoke_test();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Phase 5 bring-up done; idling");
        ESP_ERROR_CHECK(bsp_led_set_rgb(0, 255, 0));
    } else {
        ESP_LOGE(TAG, "Phase 5 bring-up failed: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(bsp_led_set_rgb(255, 0, 0));
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
