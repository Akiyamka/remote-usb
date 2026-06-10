// SPDX-License-Identifier: MIT

#include "webfs.h"

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "webfs";
static const char *PARTITION_LABEL = "webfs";
static const char *MOUNT_POINT = "/web";

esp_err_t webfs_mount(void)
{
    if (esp_littlefs_mounted(PARTITION_LABEL)) {
        ESP_LOGI(TAG, "littlefs partition already mounted at %s", MOUNT_POINT);
        return ESP_OK;
    }

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = MOUNT_POINT,
        .partition_label = PARTITION_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "littlefs partition mount failed: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "littlefs partition mounted at %s", MOUNT_POINT);
    return ESP_OK;
}

const char *webfs_root(void)
{
    return MOUNT_POINT;
}
