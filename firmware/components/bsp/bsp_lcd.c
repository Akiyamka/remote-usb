// SPDX-License-Identifier: MIT
//
// ST7735 panel driver + minimal text rasteriser.
//
// The vendor-specific init lives in `esp_lcd_st7735.c` (ported verbatim from
// the working wi-fi-drive Arduino reference). Here we set up the SPI bus,
// hand the panel to the IDF `esp_lcd` HAL, configure landscape orientation
// with the 0.96" 80x160 IPS panel's well-known gaps (x=1, y=26), and then
// add a tiny 1 bpp pixel-font rasteriser on top.
//
// The rasteriser draws one character at a time into a small static tile
// and ships the tile via `esp_lcd_panel_draw_bitmap()`. That avoids any
// per-pixel SPI overhead and keeps RAM use predictable.

#include "bsp_lcd.h"
#include "bsp_pins.h"
#include "esp_lcd_st7735.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "bsp_lcd";

#define BSP_LCD_SPI_HOST    SPI2_HOST
#define BSP_LCD_PIXEL_HZ    (40 * 1000 * 1000)
#define BSP_LCD_CMD_BITS    8
#define BSP_LCD_PARAM_BITS  8

// Gap values for the 0.96" 80x160 IPS panel (see wi-fi-drive reference).
// In landscape orientation (`swap_xy=true`) the panel's native row offset
// of 1 px and column offset of 26 px map to `set_gap(x=1, y=26)`.
#define BSP_LCD_X_GAP       1
#define BSP_LCD_Y_GAP       26

// Backlight is wired through a P-channel high-side MOSFET, so the GPIO is
// asserted LOW to turn it on. Confirmed against the wi-fi-drive config.
#define BSP_LCD_BL_ON_LEVEL  0
#define BSP_LCD_BL_OFF_LEVEL 1

// Worst-case glyph cell for the generated pixel fonts is Cairopixel 32:
// advance 20 px, tight line height 20 px. Round up to leave a little room
// for regeneration changes without needing a re-tune.
#define BSP_LCD_TILE_MAX_W   24
#define BSP_LCD_TILE_MAX_H   24
#define BSP_LCD_TILE_PIXELS  (BSP_LCD_TILE_MAX_W * BSP_LCD_TILE_MAX_H)

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static bool s_inited;

// `esp_lcd_panel_draw_bitmap()` on SPI is asynchronous: the call queues a
// CASET/RASET/RAMWR triple and returns while DMA is still streaming the
// pixel buffer out to the panel. Because we reuse a single tile in BSS
// across glyphs, returning to the caller would let it overwrite the buffer
// while DMA is still reading it — producing artefacts that look exactly
// like “bottom third of every middle character is missing”. We register a
// transfer-done callback and block on a binary semaphore after each
// draw_bitmap so the tile is safe to reuse by the time we return.
static SemaphoreHandle_t s_trans_done;

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_trans_done, &hp_task_woken);
    return hp_task_woken == pdTRUE;
}

static esp_err_t draw_bitmap_sync(int x_start, int y_start, int x_end, int y_end,
                                  const void *color_data)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel,
                                              x_start, y_start,
                                              x_end,   y_end,
                                              color_data);
    if (err != ESP_OK) {
        return err;
    }
    // Wait for DMA to finish reading the tile before letting the caller
    // overwrite it for the next character. The callback fires from the SPI
    // ISR exactly once per draw_bitmap.
    if (xSemaphoreTake(s_trans_done, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "draw_bitmap_sync: timeout waiting for transfer");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// Reused scratch tile for glyph rasterisation. Sits in BSS so we don't
// touch the heap during text rendering. Entries are stored in *big-endian*
// RGB565 — see `to_be565()` for the why.
static uint16_t s_tile[BSP_LCD_TILE_PIXELS];

// ST7735 clocks RGB565 pixels out high-byte first, but the Xtensa core is
// little-endian and our render math produces native uint16_t. Swap the two
// bytes so the bit pattern in memory matches what the panel will read off
// the wire.
static inline uint16_t to_be565(uint16_t host)
{
    return (uint16_t)((host >> 8) | (host << 8));
}

// ---------------------------------------------------------------------------
// Initialisation
// ---------------------------------------------------------------------------

static esp_err_t init_backlight_gpio(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BSP_LCD_LEDA,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "backlight gpio config");
    // Keep dark until the panel is initialised — avoids a flash of garbage.
    return gpio_set_level(BSP_LCD_LEDA, BSP_LCD_BL_OFF_LEVEL);
}

esp_err_t bsp_lcd_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(init_backlight_gpio(), TAG, "backlight gpio");

    const spi_bus_config_t bus_cfg = ST7735_PANEL_BUS_SPI_CONFIG(
        BSP_LCD_SCL, BSP_LCD_SDA,
        BSP_LCD_WIDTH * BSP_LCD_HEIGHT * (int)sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    if (s_trans_done == NULL) {
        s_trans_done = xSemaphoreCreateBinary();
        if (s_trans_done == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BSP_LCD_CS,
        .dc_gpio_num = BSP_LCD_RS,
        .spi_mode = 0,
        .pclk_hz = BSP_LCD_PIXEL_HZ,
        // queue_depth = 1 means one outstanding color transaction at a
        // time. Together with the semaphore wait below this gives strict
        // back-pressure: the caller is blocked until each tile has fully
        // shipped, so the shared `s_tile` can be safely reused.
        .trans_queue_depth = 1,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = BSP_LCD_CMD_BITS,
        .lcd_param_bits = BSP_LCD_PARAM_BITS,
        // NOTE: IDF v5.3 doesn't expose a `swap_color_bytes` flag on this
        // struct (added in later versions), so the renderer swaps RGB565
        // bytes itself before populating the tile buffer. See `to_be565()`.
        .flags = {
            .dc_low_on_data = 0,
            .octal_mode = 0,
            .lsb_first = 0,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST,
                                                 &io_cfg, &s_io),
                        TAG, "esp_lcd_new_panel_io_spi");

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BSP_LCD_RST,
        .color_space = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7735(s_io, &panel_cfg, &s_panel),
                        TAG, "esp_lcd_new_panel_st7735");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true),
                        TAG, "invert_color (IPS panel)");

    // Landscape orientation. Same recipe the upstream wi-fi-drive uses.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true),
                        TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, true),
                        TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, BSP_LCD_X_GAP, BSP_LCD_Y_GAP),
                        TAG, "set_gap");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true),
                        TAG, "disp_on");

    s_inited = true;
    ESP_LOGI(TAG, "ST7735 initialised (%dx%d landscape, gap=%d,%d)",
             BSP_LCD_WIDTH, BSP_LCD_HEIGHT, BSP_LCD_X_GAP, BSP_LCD_Y_GAP);

    // Clear to black before exposing the screen — avoids a brief flash of
    // whatever lingered in panel GRAM from the previous boot.
    esp_err_t err = bsp_lcd_clear(BSP_LCD_BLACK);
    if (err != ESP_OK) {
        return err;
    }
    return bsp_lcd_set_backlight(true);
}

esp_err_t bsp_lcd_set_backlight(bool on)
{
    return gpio_set_level(BSP_LCD_LEDA, on ? BSP_LCD_BL_ON_LEVEL : BSP_LCD_BL_OFF_LEVEL);
}

// ---------------------------------------------------------------------------
// Solid fills
// ---------------------------------------------------------------------------

// Push a solid rectangle to the panel, slicing along Y so we never need a
// tile bigger than `s_tile`. Caller's coordinates are already clipped to
// the visible area.
static esp_err_t fill_rect_clipped(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                   uint16_t color)
{
    if (w == 0 || h == 0) {
        return ESP_OK;
    }
    const uint16_t color_be = to_be565(color);
    uint16_t slice_h = (uint16_t)(BSP_LCD_TILE_PIXELS / w);
    if (slice_h == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint16_t y_done = 0; y_done < h; y_done = (uint16_t)(y_done + slice_h)) {
        const uint16_t this_h =
            (h - y_done) < slice_h ? (uint16_t)(h - y_done) : slice_h;
        const uint32_t total = (uint32_t)w * this_h;
        for (uint32_t i = 0; i < total; ++i) {
            s_tile[i] = color_be;
        }

        esp_err_t err = draw_bitmap_sync(x, y + y_done,
                                         x + w, y + y_done + this_h,
                                         s_tile);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t bsp_lcd_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t color)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    // Clip to display bounds.
    if (x >= (int16_t)BSP_LCD_WIDTH || y >= (int16_t)BSP_LCD_HEIGHT) return ESP_OK;
    if (x < 0) { w = (w > (uint16_t)(-x)) ? (uint16_t)(w + x) : 0; x = 0; }
    if (y < 0) { h = (h > (uint16_t)(-y)) ? (uint16_t)(h + y) : 0; y = 0; }
    if (x + w > BSP_LCD_WIDTH)  w = (uint16_t)(BSP_LCD_WIDTH  - x);
    if (y + h > BSP_LCD_HEIGHT) h = (uint16_t)(BSP_LCD_HEIGHT - y);
    return fill_rect_clipped(x, y, w, h, color);
}

esp_err_t bsp_lcd_clear(uint16_t color)
{
    return bsp_lcd_fill_rect(0, 0, BSP_LCD_WIDTH, BSP_LCD_HEIGHT, color);
}

// ---------------------------------------------------------------------------
// Glyph rasterisation
// ---------------------------------------------------------------------------

static int16_t align_to_font_grid(int16_t value, font_size_t font)
{
    const int16_t grid = (int16_t)fonts_grid_px(font);
    if (grid <= 1) {
        return value;
    }

    const int16_t mod = (int16_t)(value % grid);
    if (mod == 0) {
        return value;
    }
    return (value >= 0) ? (int16_t)(value - mod)
                        : (int16_t)(value - mod - grid);
}

// Render a single character cell (`adv_px × line_height`) into `s_tile` and
// ship it to the panel at (cell_x, cell_y). Returns OK even for code-points
// the font doesn't have (the cell becomes a plain `bg` block) so the caller
// can keep advancing the caret deterministically.
static esp_err_t draw_glyph(int16_t cell_x, int16_t cell_y,
                            font_size_t font,
                            uint32_t codepoint,
                            uint16_t fg, uint16_t bg)
{
    font_glyph_t g;
    const bool has_pixels = fonts_glyph(font, codepoint, &g);
    const int16_t line_h = fonts_line_height(font);
    const int16_t base  = fonts_base_line(font);

    // Decide the cell width. For glyphs the font doesn't have we still want
    // to advance by the canonical font width; falling back to the SPACE
    // glyph's advance keeps the layout uniform.
    int16_t adv = g.adv_px;
    if (!has_pixels) {
        font_glyph_t space;
        if (fonts_glyph(font, ' ', &space)) {
            adv = space.adv_px;
        }
        if (adv <= 0) {
            return ESP_OK;  // truly nothing to draw
        }
    }

    if (adv <= 0 || line_h <= 0) {
        return ESP_OK;
    }

    int16_t x0 = cell_x;
    int16_t y0 = cell_y;
    int16_t x1 = (int16_t)(cell_x + adv);
    int16_t y1 = (int16_t)(cell_y + line_h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int16_t)BSP_LCD_WIDTH)  x1 = BSP_LCD_WIDTH;
    if (y1 > (int16_t)BSP_LCD_HEIGHT) y1 = BSP_LCD_HEIGHT;
    if (x0 >= x1 || y0 >= y1) {
        return ESP_OK;
    }

    const uint16_t clip_w = (uint16_t)(x1 - x0);
    const uint16_t clip_h = (uint16_t)(y1 - y0);
    if (clip_w > BSP_LCD_TILE_MAX_W || clip_h > BSP_LCD_TILE_MAX_H) {
        ESP_LOGW(TAG, "glyph clip %dx%d exceeds tile %dx%d",
                 clip_w, clip_h, BSP_LCD_TILE_MAX_W, BSP_LCD_TILE_MAX_H);
        return bsp_lcd_fill_rect(x0, y0, clip_w, clip_h, bg);
    }

    const uint16_t bg_be = to_be565(bg);
    const uint16_t fg_be = to_be565(fg);
    const uint32_t total = (uint32_t)clip_w * (uint32_t)clip_h;
    for (uint32_t i = 0; i < total; ++i) {
        s_tile[i] = bg_be;
    }

    if (has_pixels && g.box_w > 0 && g.box_h > 0) {
        // LVGL convention: ofs_x is from the caret to the left of the glyph
        // box; ofs_y is from the baseline to the *bottom* of the glyph box
        // (positive points up). So the glyph's top edge inside the cell is
        // at (ascent - ofs_y - box_h), where ascent = line_h - base.
        const int16_t ascent = (int16_t)(line_h - base);
        const int16_t glyph_top  = (int16_t)(ascent - g.ofs_y - g.box_h);
        const int16_t glyph_left = (int16_t)(g.ofs_x);
        const int16_t clip_cell_x0 = (int16_t)(x0 - cell_x);
        const int16_t clip_cell_y0 = (int16_t)(y0 - cell_y);
        const int16_t clip_cell_x1 = (int16_t)(clip_cell_x0 + clip_w);
        const int16_t clip_cell_y1 = (int16_t)(clip_cell_y0 + clip_h);

        for (uint16_t py = 0; py < g.box_h; ++py) {
            const int16_t ty = (int16_t)(glyph_top + py);
            if (ty < clip_cell_y0 || ty >= clip_cell_y1) continue;
            for (uint16_t px = 0; px < g.box_w; ++px) {
                const int16_t tx = (int16_t)(glyph_left + px);
                if (tx < clip_cell_x0 || tx >= clip_cell_x1) continue;
                if (!fonts_pixel_on(g.bitmap, g.box_w, g.bpp, px, py)) continue;
                const uint16_t out_x = (uint16_t)(tx - clip_cell_x0);
                const uint16_t out_y = (uint16_t)(ty - clip_cell_y0);
                s_tile[(uint32_t)out_y * clip_w + out_x] = fg_be;
            }
        }
    }

    return draw_bitmap_sync(x0, y0, x1, y1, s_tile);
}

// Compute the on-screen width of a string (no actual drawing).
static int16_t measure_text(font_size_t font, const char *str)
{
    if (str == NULL) return 0;
    int16_t w = 0;
    for (const char *p = str; *p; ++p) {
        font_glyph_t g;
        if (fonts_glyph(font, (uint8_t)*p, &g)) {
            w = (int16_t)(w + g.adv_px);
        } else {
            // Use SPACE width as the canonical fallback.
            font_glyph_t sp;
            if (fonts_glyph(font, ' ', &sp)) {
                w = (int16_t)(w + sp.adv_px);
            }
        }
    }
    return w;
}

int16_t bsp_lcd_measure_text(font_size_t font, const char *str)
{
    return measure_text(font, str);
}

esp_err_t bsp_lcd_draw_text(int16_t x, int16_t y, font_size_t font,
                            const char *str, uint16_t fg, uint16_t bg)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;
    if (str == NULL) return ESP_OK;

    const int16_t line_h = fonts_line_height(font);
    if (line_h <= 0) return ESP_ERR_INVALID_ARG;
    x = align_to_font_grid(x, font);
    y = align_to_font_grid(y, font);

    // Early-out if the line is entirely off-screen vertically.
    if (y >= (int16_t)BSP_LCD_HEIGHT || y + line_h <= 0) return ESP_OK;

    int16_t caret = x;
    for (const char *p = str; *p; ++p) {
        font_glyph_t g;
        const bool ok = fonts_glyph(font, (uint8_t)*p, &g);
        int16_t adv = ok ? g.adv_px : 0;
        if (!ok) {
            font_glyph_t sp;
            if (fonts_glyph(font, ' ', &sp)) {
                adv = sp.adv_px;
            }
        }
        if (adv <= 0) continue;

        if (caret + adv > 0 && caret < (int16_t)BSP_LCD_WIDTH) {
            esp_err_t err = draw_glyph(caret, y, font, (uint8_t)*p, fg, bg);
            if (err != ESP_OK) return err;
        }
        caret = (int16_t)(caret + adv);
        if (caret >= (int16_t)BSP_LCD_WIDTH) break;
    }
    return ESP_OK;
}

esp_err_t bsp_lcd_draw_text_centered(int16_t y, font_size_t font,
                                     const char *str,
                                     uint16_t fg, uint16_t bg)
{
    const int16_t w = measure_text(font, str);
    int16_t x = (int16_t)(((int16_t)BSP_LCD_WIDTH - w) / 2);
    if (x < 0) x = 0;
    return bsp_lcd_draw_text(x, y, font, str, fg, bg);
}
