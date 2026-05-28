// SPDX-License-Identifier: MIT
//
// T-Dongle S3 pin mapping — verbatim from spec.md §2.
// This header is the single source of truth for GPIO assignments; every
// other component must pull these macros in instead of hard-coding numbers.

#pragma once

#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- SD card (SDMMC, 4-bit) ---
#define BSP_SD_CMD      GPIO_NUM_16
#define BSP_SD_CLK      GPIO_NUM_12
#define BSP_SD_D0       GPIO_NUM_14
#define BSP_SD_D1       GPIO_NUM_17
#define BSP_SD_D2       GPIO_NUM_21
#define BSP_SD_D3       GPIO_NUM_18

// --- USB OTG (native on S3, hardwired) ---
// D+ = GPIO20, D- = GPIO19 — not exposed as macros because the USB peripheral
// is hardwired and the IDF/TinyUSB driver does not accept overrides.

// --- LCD (ST7735, 80x160 portrait) ---
#define BSP_LCD_RST     GPIO_NUM_1
#define BSP_LCD_RS      GPIO_NUM_2   // DC (data/command select)
#define BSP_LCD_SDA     GPIO_NUM_3   // MOSI
#define BSP_LCD_SCL     GPIO_NUM_5   // SCLK
#define BSP_LCD_CS      GPIO_NUM_4
#define BSP_LCD_LEDA    GPIO_NUM_38  // backlight via MOSFET Q1

// --- APA102 RGB LED (on-board) ---
#define BSP_LED_DI      GPIO_NUM_40
#define BSP_LED_CI      GPIO_NUM_39

// --- User button ---
#define BSP_BTN         GPIO_NUM_0

#ifdef __cplusplus
}
#endif
