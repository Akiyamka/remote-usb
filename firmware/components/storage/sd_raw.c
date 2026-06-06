// SPDX-License-Identifier: MIT
//
// Raw SDMMC access for USB MSC. This module deliberately does not mount FATFS;
// it exposes sector reads/writes directly through the IDF sdmmc command layer.

#include "sd_raw.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

static const char *TAG = "sd_raw";

static sdmmc_card_t *s_card;
static SemaphoreHandle_t s_mutex;
static bool s_initialized;

static sdmmc_slot_config_t sd_raw_slot_config(void)
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

static bool sd_raw_range_valid(uint32_t lba, uint32_t count)
{
    if (s_card == NULL) {
        return false;
    }
    const uint32_t capacity = s_card->csd.capacity;
    return lba < capacity && count <= capacity - lba;
}

esp_err_t sd_raw_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_initialized) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.flags = SDMMC_HOST_FLAG_4BIT;

    sdmmc_slot_config_t slot_config = sd_raw_slot_config();

    esp_err_t ret = sdmmc_host_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_mutex);
        return ret;
    }

    ret = sdmmc_host_init_slot(host.slot, &slot_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_init_slot: %s", esp_err_to_name(ret));
        sdmmc_host_deinit();
        xSemaphoreGive(s_mutex);
        return ret;
    }

    s_card = heap_caps_calloc(1, sizeof(sdmmc_card_t),
                              MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (s_card == NULL) {
        sdmmc_host_deinit();
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    ret = sdmmc_card_init(&host, s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init: %s", esp_err_to_name(ret));
        free(s_card);
        s_card = NULL;
        sdmmc_host_deinit();
        xSemaphoreGive(s_mutex);
        return ret;
    }

    sdmmc_card_print_info(stdout, s_card);

    const uint32_t sector_count = s_card->csd.capacity;
    const uint16_t sector_size = (uint16_t)s_card->csd.sector_size;
    const int bus_width = (s_card->host.flags & SDMMC_HOST_FLAG_4BIT) ? 4 : 1;

    ESP_LOGI(TAG, "Card in %d-bit, freq=%uMHz, capacity %" PRIu32 " sectors",
             bus_width, (unsigned)(host.max_freq_khz / 1000), sector_count);
    ESP_LOGI(TAG, "Sector size=%" PRIu16 " bytes", sector_size);

    if (bus_width != 4) {
        ESP_LOGW(TAG, "card is not in 4-bit mode; performance will be limited");
    }

    s_initialized = true;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t sd_raw_deinit(void)
{
    if (s_mutex == NULL) {
        return ESP_OK;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!s_initialized) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    free(s_card);
    s_card = NULL;
    s_initialized = false;

    esp_err_t ret = sdmmc_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_host_deinit: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

uint32_t sd_raw_get_sector_count(void)
{
    if (!s_initialized || s_card == NULL) {
        return 0;
    }
    return s_card->csd.capacity;
}

uint16_t sd_raw_get_sector_size(void)
{
    if (!s_initialized || s_card == NULL) {
        return 0;
    }
    return (uint16_t)s_card->csd.sector_size;
}

esp_err_t sd_raw_read_sectors(void *buf, uint32_t lba, uint32_t count)
{
    if (!s_initialized || s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!sd_raw_range_valid(lba, count)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = sdmmc_read_sectors(s_card, buf, lba, count);
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t sd_raw_write_sectors(const void *buf, uint32_t lba, uint32_t count)
{
    if (!s_initialized || s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!sd_raw_range_valid(lba, count)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = sdmmc_write_sectors(s_card, buf, lba, count);
    xSemaphoreGive(s_mutex);
    return ret;
}

bool sd_raw_is_ready(void)
{
    return s_initialized && s_card != NULL;
}

esp_err_t sd_raw_sync(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
}
