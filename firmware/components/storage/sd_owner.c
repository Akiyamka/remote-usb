// SPDX-License-Identifier: MIT
//
// SD card ownership arbiter. This is the single transition point between
// FATFS ownership and raw sector ownership for USB MSC.

#include "sd_owner.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_fatfs.h"
#include "sd_raw.h"

static const char *TAG = "sd_owner";

static sd_owner_t s_owner = SD_OWNER_NONE;
static SemaphoreHandle_t s_owner_mutex;

// Phase 3 deliberately keeps `storage` independent from the future `usb_msc`
// component. These weak hooks are no-ops until phase 4 provides strong
// definitions with the same names.
void __attribute__((weak)) usb_msc_set_media_present(bool present)
{
    ESP_LOGD(TAG, "usb_msc_set_media_present(%d): no USB MSC component yet",
             present);
}

bool __attribute__((weak)) usb_msc_is_busy(void)
{
    return false;
}

const char *sd_owner_name(sd_owner_t owner)
{
    switch (owner) {
    case SD_OWNER_NONE:
        return "NONE";
    case SD_OWNER_FATFS:
        return "FATFS";
    case SD_OWNER_MSC:
        return "MSC";
    default:
        return "?";
    }
}

esp_err_t sd_owner_init(void)
{
    if (s_owner_mutex != NULL) {
        return ESP_OK;
    }

    s_owner_mutex = xSemaphoreCreateMutex();
    if (s_owner_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_owner = SD_OWNER_NONE;
    return ESP_OK;
}

sd_owner_t sd_owner_current(void)
{
    return s_owner;
}

esp_err_t sd_owner_switch_to_msc(void)
{
    esp_err_t ret = sd_owner_init();
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_owner_mutex, portMAX_DELAY);

    if (s_owner == SD_OWNER_MSC) {
        xSemaphoreGive(s_owner_mutex);
        return ESP_OK;
    }

    if (s_owner == SD_OWNER_FATFS) {
        ret = sd_fatfs_deinit();
        if (ret != ESP_OK) {
            xSemaphoreGive(s_owner_mutex);
            return ret;
        }
        s_owner = SD_OWNER_NONE;
    }

    ret = sd_raw_init();
    if (ret != ESP_OK) {
        s_owner = SD_OWNER_NONE;
        xSemaphoreGive(s_owner_mutex);
        return ret;
    }

    s_owner = SD_OWNER_MSC;
    usb_msc_set_media_present(true);

    ESP_LOGI(TAG, "owner -> %s", sd_owner_name(s_owner));
    xSemaphoreGive(s_owner_mutex);
    return ESP_OK;
}

esp_err_t sd_owner_switch_to_fatfs(void)
{
    esp_err_t ret = sd_owner_init();
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_owner_mutex, portMAX_DELAY);

    if (s_owner == SD_OWNER_FATFS) {
        xSemaphoreGive(s_owner_mutex);
        return ESP_OK;
    }

    if (s_owner == SD_OWNER_MSC) {
        if (usb_msc_is_busy()) {
            xSemaphoreGive(s_owner_mutex);
            return ESP_ERR_INVALID_STATE;
        }

        usb_msc_set_media_present(false);
        vTaskDelay(pdMS_TO_TICKS(200));

        ret = sd_raw_sync();
        if (ret != ESP_OK) {
            xSemaphoreGive(s_owner_mutex);
            return ret;
        }

        ret = sd_raw_deinit();
        if (ret != ESP_OK) {
            s_owner = SD_OWNER_NONE;
            xSemaphoreGive(s_owner_mutex);
            return ret;
        }
        s_owner = SD_OWNER_NONE;
    }

    ret = sd_fatfs_init();
    if (ret != ESP_OK) {
        s_owner = SD_OWNER_NONE;
        xSemaphoreGive(s_owner_mutex);
        return ret;
    }

    s_owner = SD_OWNER_FATFS;
    ESP_LOGI(TAG, "owner -> %s", sd_owner_name(s_owner));
    xSemaphoreGive(s_owner_mutex);
    return ESP_OK;
}

esp_err_t sd_owner_release(void)
{
    esp_err_t ret = sd_owner_init();
    if (ret != ESP_OK) {
        return ret;
    }

    xSemaphoreTake(s_owner_mutex, portMAX_DELAY);

    switch (s_owner) {
    case SD_OWNER_NONE:
        xSemaphoreGive(s_owner_mutex);
        return ESP_OK;

    case SD_OWNER_FATFS:
        ret = sd_fatfs_deinit();
        break;

    case SD_OWNER_MSC:
        if (usb_msc_is_busy()) {
            xSemaphoreGive(s_owner_mutex);
            return ESP_ERR_INVALID_STATE;
        }
        usb_msc_set_media_present(false);
        vTaskDelay(pdMS_TO_TICKS(200));
        ret = sd_raw_sync();
        if (ret == ESP_OK) {
            ret = sd_raw_deinit();
        }
        break;

    default:
        ret = ESP_ERR_INVALID_STATE;
        break;
    }

    if (ret == ESP_OK) {
        s_owner = SD_OWNER_NONE;
        ESP_LOGI(TAG, "owner -> %s", sd_owner_name(s_owner));
    }

    xSemaphoreGive(s_owner_mutex);
    return ret;
}
