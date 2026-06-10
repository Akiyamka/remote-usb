// SPDX-License-Identifier: MIT

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webfs_mount(void);
const char *webfs_root(void);

#ifdef __cplusplus
}
#endif
