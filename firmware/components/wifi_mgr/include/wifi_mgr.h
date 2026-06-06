// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "wifi_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool connected;
    char ssid[33];
    char ip_str[16];
    int8_t rssi;
} wifi_status_t;

esp_err_t wifi_mgr_init(void);
esp_err_t wifi_mgr_connect(const wifi_creds_t *creds, uint32_t timeout_ms);
esp_err_t wifi_mgr_disconnect(void);
void wifi_mgr_get_status(wifi_status_t *out);

#ifdef __cplusplus
}
#endif
