// SPDX-License-Identifier: MIT
//
// FATFS mount wrapper for HTTP-mode access to the SD card.

#include "sd_fatfs.h"

#include <inttypes.h>

#include "bsp_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_fatfs";
static const char *MOUNT_POINT = "/sdcard";

static sdmmc_card_t *s_card;

static sdmmc_slot_config_t sd_fatfs_slot_config(void)
{
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = BSP_SD_CLK;
    slot_config.cmd = BSP_SD_CMD;
    slot_config.d0 = BSP_SD_D0;
    slot_config.d1 = BSP_SD_D1;
    slot_config.d2 = BSP_SD_D2;
    slot_config.d3 = BSP_SD_D3;
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    return slot_config;
}

esp_err_t sd_fatfs_init(void)
{
    if (s_card != NULL) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.flags = SDMMC_HOST_FLAG_4BIT;

    sdmmc_slot_config_t slot_config = sd_fatfs_slot_config();

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mount %s failed: %s", MOUNT_POINT, esp_err_to_name(ret));
        s_card = NULL;
        return ret;
    }

    const int bus_width = (s_card->host.flags & SDMMC_HOST_FLAG_4BIT) ? 4 : 1;
    ESP_LOGI(TAG, "Mounted %s: card in %d-bit, freq=%uMHz, capacity %" PRIu32 " sectors",
             MOUNT_POINT, bus_width, (unsigned)(host.max_freq_khz / 1000),
             (uint32_t)s_card->csd.capacity);

    if (bus_width != 4) {
        ESP_LOGW(TAG, "card is not in 4-bit mode; performance will be limited");
    }

    return ESP_OK;
}

esp_err_t sd_fatfs_deinit(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "unmount %s failed: %s", MOUNT_POINT, esp_err_to_name(ret));
        return ret;
    }

    ret = sdmmc_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_deinit failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Unmounted %s", MOUNT_POINT);
    return ESP_OK;
}

const char *sd_fatfs_mount_point(void)
{
    return MOUNT_POINT;
}

uint64_t sd_fatfs_get_total_bytes(void)
{
    if (s_card == NULL) {
        return 0;
    }

    uint64_t total = 0;
    uint64_t free = 0;
    esp_err_t ret = esp_vfs_fat_info(MOUNT_POINT, &total, &free);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "fat info total failed: %s", esp_err_to_name(ret));
        return 0;
    }
    return total;
}

uint64_t sd_fatfs_get_free_bytes(void)
{
    if (s_card == NULL) {
        return 0;
    }

    uint64_t total = 0;
    uint64_t free = 0;
    esp_err_t ret = esp_vfs_fat_info(MOUNT_POINT, &total, &free);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "fat info free failed: %s", esp_err_to_name(ret));
        return 0;
    }
    return free;
}
