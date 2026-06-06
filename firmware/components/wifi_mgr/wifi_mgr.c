// SPDX-License-Identifier: MIT
//
// ESP32-S3 Wi-Fi station manager.

#include "wifi_mgr.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT  (1 << 0)

static EventGroupHandle_t s_event_group;
static esp_netif_t *s_sta_netif;
static bool s_initialized;
static bool s_started;
static bool s_reconnect_enabled;
static wifi_status_t s_status;

static esp_err_t validate_creds(const wifi_creds_t *creds)
{
    if (creds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strlen(creds->ssid);
    const size_t password_len = strlen(creds->password);

    if (ssid_len < 1 || ssid_len > 32) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password_len != 0 && (password_len < 8 || password_len > 63)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

static void copy_ssid(char dst[33], const char *ssid)
{
    const size_t len = strnlen(ssid, 32);
    memcpy(dst, ssid, len);
    dst[len] = '\0';
}

static void set_disconnected_status(void)
{
    s_status.connected = false;
    s_status.ip_str[0] = '\0';
    s_status.rssi = 0;
}

static void update_ap_status(void)
{
    wifi_ap_record_t ap = {0};
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap);
    if (ret == ESP_OK) {
        copy_ssid(s_status.ssid, (const char *)ap.ssid);
        s_status.rssi = ap.rssi;
    } else {
        s_status.rssi = 0;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(ret));
        }
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        set_disconnected_status();
        if (s_event_group != NULL) {
            xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
        }

        ESP_LOGW(TAG, "Disconnected, reason=%d",
                 event != NULL ? event->reason : -1);

        if (s_reconnect_enabled) {
            esp_err_t ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "reconnect failed: %s", esp_err_to_name(ret));
            }
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        s_status.connected = true;
        snprintf(s_status.ip_str, sizeof(s_status.ip_str), IPSTR,
                 IP2STR(&event->ip_info.ip));
        update_ap_status();

        ESP_LOGI(TAG, "Got IP: %s", s_status.ip_str);
        if (s_event_group != NULL) {
            xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t wifi_mgr_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi manager initialised");
    return ESP_OK;
}

esp_err_t wifi_mgr_connect(const wifi_creds_t *creds, uint32_t timeout_ms)
{
    esp_err_t ret = validate_creds(creds);
    if (ret != ESP_OK) {
        return ret;
    }
    if (timeout_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = wifi_mgr_init();
    if (ret != ESP_OK) {
        return ret;
    }

    if (s_started) {
        ret = wifi_mgr_disconnect();
        if (ret != ESP_OK) {
            return ret;
        }
    }

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, creds->ssid, strlen(creds->ssid));
    memcpy(wifi_config.sta.password, creds->password, strlen(creds->password));
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        return ret;
    }

    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
    memset(&s_status, 0, sizeof(s_status));
    copy_ssid(s_status.ssid, creds->ssid);

    s_reconnect_enabled = true;
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        s_reconnect_enabled = false;
        return ret;
    }
    s_started = true;

    ESP_LOGI(TAG, "Connecting to SSID '%s'", creds->ssid);
    EventBits_t bits = xEventGroupWaitBits(s_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Connect timeout after %" PRIu32 " ms", timeout_ms);
    s_reconnect_enabled = false;
    (void)esp_wifi_disconnect();
    (void)esp_wifi_stop();
    s_started = false;
    set_disconnected_status();
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_mgr_disconnect(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    s_reconnect_enabled = false;
    if (s_event_group != NULL) {
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);
    }

    esp_err_t first_err = ESP_OK;
    if (s_started) {
        esp_err_t ret = esp_wifi_disconnect();
        if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
            first_err = ret;
        }

        ret = esp_wifi_stop();
        if (first_err == ESP_OK && ret != ESP_OK &&
            ret != ESP_ERR_WIFI_NOT_STARTED) {
            first_err = ret;
        }
        s_started = false;
    }

    set_disconnected_status();
    return first_err;
}

void wifi_mgr_get_status(wifi_status_t *out)
{
    if (out == NULL) {
        return;
    }

    if (s_status.connected) {
        update_ap_status();
    }
    *out = s_status;
}
