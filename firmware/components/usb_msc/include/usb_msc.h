// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t usb_msc_init(void);
void usb_msc_set_media_present(bool present);
bool usb_msc_is_busy(void);

#ifdef __cplusplus
}
#endif
