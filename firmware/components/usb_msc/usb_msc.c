// SPDX-License-Identifier: MIT
//
// TinyUSB MSC bridge backed by the raw SDMMC driver.

#include "usb_msc.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "sd_raw.h"
#include "tinyusb.h"
#include "tusb.h"

static const char *TAG = "usb_msc";

extern tusb_desc_device_t const usb_msc_desc_device;
extern char const *usb_msc_string_desc_arr[];
extern const size_t usb_msc_string_desc_count;
extern uint8_t const usb_msc_desc_configuration[];

static volatile bool s_media_present;
static volatile uint32_t s_io_in_progress;
static volatile uint32_t s_last_io_ms;
static bool s_initialized;

static bool io_args_valid(uint32_t offset, uint32_t bufsize)
{
    const uint16_t sector_size = sd_raw_get_sector_size();
    return sector_size != 0 &&
           offset == 0 &&
           bufsize != 0 &&
           (bufsize % sector_size) == 0;
}

esp_err_t usb_msc_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    static char serial_str[13];
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_efuse_mac_get_default(mac), TAG, "read default MAC");
    snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    usb_msc_string_desc_arr[3] = serial_str;

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &usb_msc_desc_device,
        .string_descriptor = usb_msc_string_desc_arr,
        .string_descriptor_count = (int)usb_msc_string_desc_count,
        .external_phy = false,
        .configuration_descriptor = usb_msc_desc_configuration,
    };

    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG,
                        "tinyusb_driver_install");

    s_initialized = true;
    ESP_LOGI(TAG, "TinyUSB MSC initialized serial=%s", serial_str);
    return ESP_OK;
}

void usb_msc_connect_to_host(void)
{
    if (!s_initialized) {
        return;
    }

    tud_connect();
    ESP_LOGI(TAG, "USB host connection enabled");
}

void usb_msc_disconnect_from_host(void)
{
    if (!s_initialized) {
        return;
    }

    tud_disconnect();
    ESP_LOGI(TAG, "USB host connection disabled");
}

void usb_msc_set_media_present(bool present)
{
    s_media_present = present;
    ESP_LOGI(TAG, "Media presence: %s", present ? "INSERTED" : "REMOVED");
}

bool usb_msc_is_busy(void)
{
    if (s_io_in_progress > 0) {
        return true;
    }
    if (s_last_io_ms == 0) {
        return false;
    }

    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    return (uint32_t)(now_ms - s_last_io_ms) < 500;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;

    const char vid[] = "MyDevice";
    const char pid[] = "Wireless Drive  ";
    const char rev[] = "1.00";
    memcpy(vendor_id, vid, sizeof(vid) - 1);
    memcpy(product_id, pid, sizeof(pid) - 1);
    memcpy(product_rev, rev, sizeof(rev) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    if (!s_media_present) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }

    const bool ready = sd_raw_is_ready();
    if (!ready) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    }
    return ready;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size)
{
    (void)lun;

    if (!s_media_present || !sd_raw_is_ready()) {
        *block_count = 0;
        *block_size = 0;
        return;
    }

    *block_count = sd_raw_get_sector_count();
    *block_size = sd_raw_get_sector_size();
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start,
                           bool load_eject)
{
    (void)power_condition;

    if (load_eject && !start) {
        ESP_LOGI(TAG, "Host requested eject");
        if (s_media_present) {
            sd_raw_sync();
        }
    }

    (void)lun;
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize)
{
    (void)lun;

    if (!s_media_present || !sd_raw_is_ready() ||
        !io_args_valid(offset, bufsize)) {
        return TUD_MSC_RET_ERROR;
    }

    s_io_in_progress++;
    const uint32_t sectors = bufsize / sd_raw_get_sector_size();

    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = sd_raw_read_sectors(buffer, lba, sectors);
    const int64_t dt = esp_timer_get_time() - t0;
    s_last_io_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_io_in_progress--;

    if (dt > 50000) {
        ESP_LOGW(TAG, "Slow read lba=%" PRIu32 " cnt=%" PRIu32 " dt=%lldus",
                 lba, sectors, (long long)dt);
    }

    return (err == ESP_OK) ? (int32_t)bufsize : TUD_MSC_RET_ERROR;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;

    if (!s_media_present || !sd_raw_is_ready() ||
        !io_args_valid(offset, bufsize)) {
        return TUD_MSC_RET_ERROR;
    }

    s_io_in_progress++;
    const uint32_t sectors = bufsize / sd_raw_get_sector_size();

    const esp_err_t err = sd_raw_write_sectors(buffer, lba, sectors);
    s_last_io_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_io_in_progress--;
    return (err == ESP_OK) ? (int32_t)bufsize : TUD_MSC_RET_ERROR;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void *buffer, uint16_t bufsize)
{
    (void)scsi_cmd;
    (void)buffer;
    (void)bufsize;

    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return TUD_MSC_RET_ERROR;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    return s_media_present && sd_raw_is_ready();
}
