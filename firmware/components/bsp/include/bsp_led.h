// SPDX-License-Identifier: MIT
//
// APA102 (single on-board LED) driver.
//
// The T-Dongle S3 has exactly one APA102 LED wired to SPI-like pins
// DI=GPIO40, CI=GPIO39. We drive it via SPI2_HOST with the LCD on SPI3 to
// avoid bus arbitration. Brightness is a 5-bit field in the APA102 frame
// (0..31); the public API takes a separate `brightness` argument and clamps.
//
// Public API per plan.md Phase 1:
//   bsp_led_init(), bsp_led_set_rgb(r,g,b), bsp_led_off().
// A `bsp_led_set_rgb_brightness()` overload is provided because future
// phases (STATE_BOOT blink white, STATE_USB_MODE solid blue, etc.) will
// want different intensities without changing colour mixing.

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise SPI2_HOST and configure the APA102 transmit slot.
// Must be called before any of the setters below. Safe to call multiple
// times — subsequent calls return ESP_OK without re-initialising.
esp_err_t bsp_led_init(void);

// Set RGB colour at the default brightness (a moderate ~25% of full scale,
// chosen so the very bright APA102 doesn't blind users during development).
// Components are 8-bit linear (0..255). Returns the SPI transmit result.
esp_err_t bsp_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);

// Same as bsp_led_set_rgb() but with explicit 5-bit brightness (0..31).
// Useful for blink animations where brightness modulation feels smoother
// than full RGB ramps.
esp_err_t bsp_led_set_rgb_brightness(uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t brightness);

// Turn the LED off (sets RGB=0,0,0 and brightness=0).
esp_err_t bsp_led_off(void);

#ifdef __cplusplus
}
#endif
