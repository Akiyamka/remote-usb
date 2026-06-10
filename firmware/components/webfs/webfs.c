// SPDX-License-Identifier: MIT
//
// Phase 6 stub. Real LittleFS mounting is implemented in phase 7.

#include "webfs.h"

#include "esp_log.h"

static const char *TAG = "webfs";
static const char *MOUNT_POINT = "/web";

esp_err_t webfs_mount(void)
{
    ESP_LOGI(TAG, "webfs_mount stub: skipping LittleFS mount");
    return ESP_OK;
}

const char *webfs_root(void)
{
    return MOUNT_POINT;
}
