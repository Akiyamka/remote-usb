// SPDX-License-Identifier: MIT

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SD_OWNER_NONE,
    SD_OWNER_FATFS,
    SD_OWNER_MSC,
} sd_owner_t;

esp_err_t sd_owner_init(void);
sd_owner_t sd_owner_current(void);
const char *sd_owner_name(sd_owner_t owner);

esp_err_t sd_owner_switch_to_fatfs(void);
esp_err_t sd_owner_switch_to_msc(void);
esp_err_t sd_owner_release(void);

#ifdef __cplusplus
}
#endif
