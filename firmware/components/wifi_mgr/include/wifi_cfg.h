// SPDX-License-Identifier: MIT

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    char password[64];
} wifi_creds_t;

typedef enum {
    WIFI_CREDS_NONE,
    WIFI_CREDS_FROM_NVS,
    WIFI_CREDS_FROM_FILE,
} wifi_creds_source_t;

esp_err_t wifi_cfg_read_from_sd(wifi_creds_t *out);
esp_err_t wifi_cfg_read_from_nvs(wifi_creds_t *out);
esp_err_t wifi_cfg_save_to_nvs(const wifi_creds_t *creds);
esp_err_t wifi_cfg_create_default(void);
esp_err_t wifi_cfg_delete_from_sd(void);

#ifdef __cplusplus
}
#endif
