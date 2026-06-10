// SPDX-License-Identifier: MIT
//
// Phase 6 stub. Real HTTP API/static serving is implemented in phase 8.

#include "http_server.h"

#include <stdbool.h>

#include "esp_log.h"

static const char *TAG = "http_server";
static bool s_started;

esp_err_t http_server_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_started = true;
    ESP_LOGI(TAG, "http_server_start stub");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    s_started = false;
    ESP_LOGI(TAG, "http_server_stop stub");
    return ESP_OK;
}
