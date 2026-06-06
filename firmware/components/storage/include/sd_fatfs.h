// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_fatfs_init(void);
esp_err_t sd_fatfs_deinit(void);
const char *sd_fatfs_mount_point(void);
uint64_t sd_fatfs_get_total_bytes(void);
uint64_t sd_fatfs_get_free_bytes(void);

#ifdef __cplusplus
}
#endif
