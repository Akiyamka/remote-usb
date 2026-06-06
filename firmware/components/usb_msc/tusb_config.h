// SPDX-License-Identifier: MIT
//
// Reference TinyUSB class configuration for this component. With Espressif's
// `esp_tinyusb` wrapper these values are applied through sdkconfig.defaults
// so the managed TinyUSB library and this component are built with the same
// class set.

#pragma once

#define CFG_TUSB_RHPORT0_MODE      OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE     64

#define CFG_TUD_MSC                1
#define CFG_TUD_CDC                0
#define CFG_TUD_HID                0

#define CFG_TUD_MSC_EP_BUFSIZE     8192
#define CFG_TUSB_DEBUG             2
