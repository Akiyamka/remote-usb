// SPDX-License-Identifier: MIT
//
// ST7735 driver for the 80x160 IPS panel on the T-Dongle S3.
//
// Sits on top of the standard ESP-IDF `esp_lcd_panel` HAL (see
// `esp_lcd_st7735.c` for the vendor-specific init sequence). We do not pull
// LVGL into the firmware; instead we rasterise text into a small RAM tile
// using the Maple Mono fonts from the `fonts` component and ship each tile
// to the panel via `esp_lcd_panel_draw_bitmap()`.
//
// Coordinate system: the panel is configured for landscape 160x80 — the
// physical orientation of the dongle when it is plugged into a horizontal
// USB port, which matches how users normally read the screen.
// Origin (0, 0) is the top-left corner.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

// Visible panel dimensions after orientation is applied.
#define BSP_LCD_WIDTH   160
#define BSP_LCD_HEIGHT  80

// Pack 8-8-8 RGB into RGB565 (R/G/B inputs are 0..255). Output is host-order
// and gets byte-swapped right before being clocked out, so the panel sees
// big-endian bytes as ST7735 expects.
#define BSP_LCD_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8) << 8) | \
                (((uint16_t)(g) & 0xFC) << 3) | \
                (((uint16_t)(b) & 0xF8) >> 3)))

#define BSP_LCD_BLACK   BSP_LCD_RGB565(0,   0,   0)
#define BSP_LCD_WHITE   BSP_LCD_RGB565(255, 255, 255)
#define BSP_LCD_RED     BSP_LCD_RGB565(255, 0,   0)
#define BSP_LCD_GREEN   BSP_LCD_RGB565(0,   255, 0)
#define BSP_LCD_BLUE    BSP_LCD_RGB565(0,   0,   255)
#define BSP_LCD_YELLOW  BSP_LCD_RGB565(255, 255, 0)
#define BSP_LCD_CYAN    BSP_LCD_RGB565(0,   255, 255)
#define BSP_LCD_GRAY    BSP_LCD_RGB565(128, 128, 128)

// Initialise SPI bus, ST7735 panel, and turn the backlight on.
// Idempotent: subsequent calls are no-ops returning ESP_OK.
esp_err_t bsp_lcd_init(void);

// Fill the entire visible area with `color` (RGB565, host order).
esp_err_t bsp_lcd_clear(uint16_t color);

// Fill an axis-aligned rectangle [x, x+w) × [y, y+h) with `color`. Useful
// for drawing solid backgrounds before rendering text on top.
esp_err_t bsp_lcd_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t color);

// Draw a NUL-terminated UTF-8 string (ASCII subset honoured) at pixel
// position (x, y) — y is the TOP of the line — using the named bitmap
// font. Each glyph cell is `adv_w x line_height` and gets cleared to `bg`
// first, with the foreground glyph alpha-blended onto it. Returns the first
// non-OK error from the underlying SPI transactions.
esp_err_t bsp_lcd_draw_text(int16_t x, int16_t y, font_size_t font,
                            const char *str, uint16_t fg, uint16_t bg);

// Same as `bsp_lcd_draw_text()` but draws the line horizontally centred in
// the panel. Returns ESP_OK and clamps to the left edge if the string is
// wider than the screen.
esp_err_t bsp_lcd_draw_text_centered(int16_t y, font_size_t font,
                                     const char *str,
                                     uint16_t fg, uint16_t bg);

// Toggle the backlight MOSFET. The T-Dongle uses a P-channel high-side
// switch, so the GPIO is asserted LOW to turn the LED on (verified against
// the upstream wi-fi-drive Arduino reference).
esp_err_t bsp_lcd_set_backlight(bool on);

#ifdef __cplusplus
}
#endif
