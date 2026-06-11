// SPDX-License-Identifier: MIT

#include "http_internal.h"

#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_fatfs.h"
#include "sd_owner.h"
#include "ui_state.h"
#include "usb_msc.h"
#include "wifi_mgr.h"

static const char *TAG = "http_api";

static SemaphoreHandle_t s_switch_mutex;
static volatile bool s_upload_in_progress;

static const char *mode_from_owner(sd_owner_t owner)
{
    switch (owner) {
    case SD_OWNER_FATFS:
        return "http";
    case SD_OWNER_MSC:
        return "usb";
    case SD_OWNER_NONE:
    default:
        return "switching";
    }
}

esp_err_t http_api_status_init(void)
{
    if (s_switch_mutex != NULL) {
        return ESP_OK;
    }

    s_switch_mutex = xSemaphoreCreateMutex();
    if (s_switch_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool http_upload_in_progress(void)
{
    return s_upload_in_progress;
}

void http_set_upload_in_progress(bool in_progress)
{
    s_upload_in_progress = in_progress;
}

esp_err_t http_handle_api_status(httpd_req_t *req)
{
    bool switching = false;
    sd_owner_t owner = sd_owner_current();

    char total_mb[24] = "null";
    char free_mb[24] = "null";
    if (s_switch_mutex != NULL &&
        xSemaphoreTake(s_switch_mutex, 0) == pdTRUE) {
        owner = sd_owner_current();
        if (owner == SD_OWNER_FATFS) {
            snprintf(total_mb, sizeof(total_mb), "%llu",
                     (unsigned long long)(sd_fatfs_get_total_bytes() /
                                          (1024ULL * 1024ULL)));
            snprintf(free_mb, sizeof(free_mb), "%llu",
                     (unsigned long long)(sd_fatfs_get_free_bytes() /
                                          (1024ULL * 1024ULL)));
        }
        xSemaphoreGive(s_switch_mutex);
    } else {
        switching = true;
    }

    const char *mode = switching ? "switching" : mode_from_owner(owner);
    const bool sd_present = owner != SD_OWNER_NONE;

    wifi_status_t wifi = {0};
    wifi_mgr_get_status(&wifi);

    char fw_version[64];
    char ssid[200];
    char ip[40];
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (http_json_escape(app_desc->version, fw_version, sizeof(fw_version)) !=
        ESP_OK ||
        http_json_escape(wifi.ssid, ssid, sizeof(ssid)) != ESP_OK ||
        http_json_escape(wifi.ip_str, ip, sizeof(ip)) != ESP_OK) {
        return http_send_500(req, "json_escape_failed");
    }

    char json[640];
    const int written = snprintf(
        json, sizeof(json),
        "{\"fw_version\":\"%s\","
        "\"api_version\":%d,"
        "\"mode\":\"%s\","
        "\"sd\":{\"present\":%s,\"total_mb\":%s,\"free_mb\":%s},"
        "\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"rssi\":%d}}",
        fw_version, HTTP_API_VERSION, mode, sd_present ? "true" : "false",
        total_mb, free_mb, wifi.connected ? "true" : "false", ssid, ip,
        (int)wifi.rssi);

    if (written < 0 || written >= (int)sizeof(json)) {
        return http_send_500(req, "status_too_large");
    }

    return http_send_json(req, "200 OK", json);
}

esp_err_t http_handle_mode_usb(httpd_req_t *req)
{
    if (xSemaphoreTake(s_switch_mutex, 0) != pdTRUE) {
        return http_send_409(req, "switch_in_progress");
    }

    if (http_upload_in_progress()) {
        xSemaphoreGive(s_switch_mutex);
        return http_send_409(req, "active_upload");
    }

    if (sd_owner_current() == SD_OWNER_MSC) {
        xSemaphoreGive(s_switch_mutex);
        return http_send_200_mode(req, "usb", true);
    }

    ui_state_show(UI_SWITCHING);
    esp_err_t ret = sd_owner_switch_to_msc();
    xSemaphoreGive(s_switch_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "switch to USB failed: %s", esp_err_to_name(ret));
        ui_state_show(UI_ERROR_GENERIC);
        return http_send_500(req, esp_err_to_name(ret));
    }

    ui_state_show(UI_MODE_USB);
    return http_send_200_mode(req, "usb", false);
}

esp_err_t http_handle_mode_http(httpd_req_t *req)
{
    if (xSemaphoreTake(s_switch_mutex, 0) != pdTRUE) {
        return http_send_409(req, "switch_in_progress");
    }

    if (sd_owner_current() == SD_OWNER_MSC && usb_msc_is_busy()) {
        xSemaphoreGive(s_switch_mutex);
        return http_send_409(req, "host_io_active");
    }

    if (sd_owner_current() == SD_OWNER_FATFS) {
        xSemaphoreGive(s_switch_mutex);
        return http_send_200_mode(req, "http", true);
    }

    ui_state_show(UI_SWITCHING);
    esp_err_t ret = sd_owner_switch_to_fatfs();
    xSemaphoreGive(s_switch_mutex);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "switch to HTTP failed: %s", esp_err_to_name(ret));
        ui_state_show(UI_ERROR_GENERIC);
        if (ret == ESP_ERR_INVALID_STATE) {
            return http_send_409(req, "host_io_active");
        }
        return http_send_500(req, esp_err_to_name(ret));
    }

    ui_state_update_memory(sd_fatfs_get_total_bytes(), sd_fatfs_get_free_bytes());
    ui_state_show(UI_MODE_HTTP);
    return http_send_200_mode(req, "http", false);
}
