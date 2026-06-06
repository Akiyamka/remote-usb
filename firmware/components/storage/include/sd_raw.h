// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_raw_init(void);
esp_err_t sd_raw_deinit(void);
uint32_t sd_raw_get_sector_count(void);
uint16_t sd_raw_get_sector_size(void);
esp_err_t sd_raw_read_sectors(void *buf, uint32_t lba, uint32_t count);
esp_err_t sd_raw_write_sectors(const void *buf, uint32_t lba, uint32_t count);
bool sd_raw_is_ready(void);
esp_err_t sd_raw_sync(void);

#ifdef __cplusplus
}
#endif
