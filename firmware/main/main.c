#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "http_server.h"
#include "sd_fatfs.h"
#include "sd_owner.h"
#include "ui_led.h"
#include "ui_state.h"
#include "usb_msc.h"
#include "webfs.h"
#include "wifi_cfg.h"
#include "wifi_mgr.h"

static const char *TAG = "main";

// Firmware revision banner shown on the welcome screen. Bumped alongside
// each spec/plan revision so we can verify on the LCD which build is
// actually flashed onto the device.
#define APP_VERSION_STR  "v0.0.8"

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

static void startup_error_loop(void)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void startup_error(ui_screen_t screen, const char *message, esp_err_t err)
{
    if (message != NULL) {
        ESP_LOGE(TAG, "%s: %s", message, esp_err_to_name(err));
    }
    ui_state_show(screen);
    startup_error_loop();
}

static void load_wifi_credentials(wifi_creds_t *creds,
                                  wifi_creds_source_t *source)
{
    *source = WIFI_CREDS_NONE;

    esp_err_t ret = wifi_cfg_read_from_sd(creds);
    if (ret == ESP_OK) {
        *source = WIFI_CREDS_FROM_FILE;
        ESP_LOGI(TAG, "Using wifi.cfg from SD");
        return;
    }

    if (ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "wifi.cfg invalid: %s", esp_err_to_name(ret));
        ui_state_show(UI_BOOT_CONFIG_INVALID);
        startup_error_loop();
    }

    ret = wifi_cfg_read_from_nvs(creds);
    if (ret == ESP_OK) {
        *source = WIFI_CREDS_FROM_NVS;
        ESP_LOGI(TAG, "Using credentials from NVS");
        return;
    }

    if (ret != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS credentials unavailable: %s", esp_err_to_name(ret));
    }

    ret = wifi_cfg_create_default();
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "Created /sdcard/wifi.cfg; fill it and reboot");
        ui_state_show(UI_BOOT_CONFIG_CREATED);
    } else {
        ESP_LOGE(TAG, "Failed to create default wifi.cfg: %s",
                 esp_err_to_name(ret));
        ui_state_show(UI_BOOT_CONFIG_INVALID);
    }

    startup_error_loop();
}

static void persist_file_credentials(const wifi_creds_t *creds,
                                     wifi_creds_source_t source)
{
    if (source != WIFI_CREDS_FROM_FILE) {
        return;
    }

    esp_err_t ret = wifi_cfg_save_to_nvs(creds);
    if (ret == ESP_OK) {
        ret = wifi_cfg_delete_from_sd();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to delete wifi.cfg after NVS save: %s",
                     esp_err_to_name(ret));
        }
        return;
    }

    ESP_LOGW(TAG, "Failed to save credentials to NVS, keeping wifi.cfg: %s",
             esp_err_to_name(ret));
    ui_led_set_special_yellow();
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot " APP_VERSION_STR);

    ESP_ERROR_CHECK(init_nvs());
    ui_state_init();
    ui_led_init();
    ui_state_show(UI_BOOT_WELCOME);

    ESP_ERROR_CHECK(usb_msc_init());
    usb_msc_set_media_present(false);

    esp_err_t ret = webfs_mount();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Web UI partition mount failed: %s", esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(1500));

    ret = sd_owner_init();
    if (ret != ESP_OK) {
        startup_error(UI_ERROR_GENERIC, "sd_owner_init failed", ret);
    }

    ret = sd_owner_switch_to_fatfs();
    if (ret != ESP_OK) {
        startup_error(UI_ERROR_SD, "SD card not detected", ret);
    }

    const uint64_t total = sd_fatfs_get_total_bytes();
    const uint64_t free = sd_fatfs_get_free_bytes();
    ui_state_update_memory(total, free);
    ui_state_show(UI_BOOT_SD_MEMORY);
    vTaskDelay(pdMS_TO_TICKS(2000));

    wifi_creds_t creds = {0};
    wifi_creds_source_t source = WIFI_CREDS_NONE;
    load_wifi_credentials(&creds, &source);

    ui_state_update_wifi(creds.ssid, "");
    ui_state_show(UI_BOOT_CONNECTING);

    ret = wifi_mgr_init();
    if (ret != ESP_OK) {
        startup_error(UI_ERROR_GENERIC, "wifi_mgr_init failed", ret);
    }

    ret = wifi_mgr_connect(&creds, 15000);
    if (ret == ESP_OK) {
        wifi_status_t status = {0};
        wifi_mgr_get_status(&status);
        ui_state_update_wifi(status.ssid, status.ip_str);
        ESP_LOGI(TAG, "Wi-Fi connected: ssid='%s' ip=%s rssi=%d",
                 status.ssid, status.ip_str, status.rssi);

        persist_file_credentials(&creds, source);

        ret = http_server_start();
        if (ret != ESP_OK) {
            startup_error(UI_ERROR_GENERIC, "http_server_start failed", ret);
        }

        ui_state_show(UI_MODE_HTTP);
        ESP_LOGI(TAG, "Startup complete: HTTP mode");
        return;
    }

    ESP_LOGW(TAG, "Wi-Fi connect failed, falling back to USB mode: %s",
             esp_err_to_name(ret));
    ui_state_show(UI_BOOT_CONNECT_FAILED);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ret = sd_owner_switch_to_msc();
    if (ret != ESP_OK) {
        startup_error(UI_ERROR_GENERIC, "sd_owner_switch_to_msc failed", ret);
    }

    ui_state_show(UI_MODE_USB);
    ESP_LOGI(TAG, "Startup complete: USB mode");
}
