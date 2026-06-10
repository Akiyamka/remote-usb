// SPDX-License-Identifier: MIT
//
// Phase 6 minimal UI. Real polished LCD rendering arrives in phase 10.

#include "ui_state.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bsp_lcd.h"
#include "esp_log.h"
#include "ui_led.h"

static const char *TAG = "ui_state";

#define APP_VERSION_STR  "v0.0.8"

static char s_ssid[33];
static char s_ip[16];
static uint64_t s_total_bytes;
static uint64_t s_free_bytes;
static bool s_lcd_ok;

const char *ui_state_name(ui_screen_t screen)
{
    switch (screen) {
    case UI_BOOT_WELCOME:
        return "BOOT_WELCOME";
    case UI_BOOT_SD_MEMORY:
        return "BOOT_SD_MEMORY";
    case UI_BOOT_CONNECTING:
        return "BOOT_CONNECTING";
    case UI_BOOT_CONFIG_INVALID:
        return "BOOT_CONFIG_INVALID";
    case UI_BOOT_CONFIG_CREATED:
        return "BOOT_CONFIG_CREATED";
    case UI_BOOT_CONNECT_FAILED:
        return "BOOT_CONNECT_FAILED";
    case UI_MODE_USB:
        return "MODE_USB";
    case UI_MODE_HTTP:
        return "MODE_HTTP";
    case UI_SWITCHING:
        return "SWITCHING";
    case UI_ERROR_SD:
        return "ERROR_SD";
    case UI_ERROR_GENERIC:
        return "ERROR_GENERIC";
    default:
        return "?";
    }
}

void ui_state_init(void)
{
    esp_err_t ret = bsp_lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_lcd_init failed: %s", esp_err_to_name(ret));
        s_lcd_ok = false;
        return;
    }

    s_lcd_ok = true;
    ESP_LOGI(TAG, "LCD UI initialised");
}

static void fit_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    const size_t len = strlen(src);
    if (len < dst_size) {
        memcpy(dst, src, len + 1);
        return;
    }

    memcpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
    if (dst_size > 2) {
        dst[dst_size - 2] = '>';
    }
}

static uint64_t bytes_to_gb(uint64_t bytes)
{
    return bytes / (1024ULL * 1024ULL * 1024ULL);
}

static void draw_centered(int16_t y, font_size_t font, const char *text,
                          uint16_t fg)
{
    if (!s_lcd_ok) {
        return;
    }

    esp_err_t ret = bsp_lcd_draw_text_centered(y, font, text, fg, BSP_LCD_BLACK);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "draw '%s' failed: %s", text, esp_err_to_name(ret));
    }
}

static void draw_wifi_mode(ui_screen_t screen)
{
    char ssid[23];
    char ip[16];
    fit_text(ssid, sizeof(ssid), s_ssid);
    fit_text(ip, sizeof(ip), s_ip[0] != '\0' ? s_ip : "No IP");

    draw_centered(5, FONT_SMALL, ssid, BSP_LCD_WHITE);
    draw_centered(24, FONT_MEDIUM, ip, BSP_LCD_GREEN);

    const char *mode = "Mode: ?";
    if (screen == UI_MODE_HTTP) {
        mode = "Mode: HTTP";
    } else if (screen == UI_MODE_USB) {
        mode = "Mode: USB";
    } else if (screen == UI_SWITCHING) {
        mode = "Switching...";
    }
    draw_centered(52, FONT_SMALL, mode, BSP_LCD_CYAN);
}

static void draw_screen(ui_screen_t screen)
{
    if (!s_lcd_ok) {
        return;
    }

    esp_err_t ret = bsp_lcd_clear(BSP_LCD_BLACK);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "clear failed: %s", esp_err_to_name(ret));
        return;
    }

    char line[32];
    char ssid[23];

    switch (screen) {
    case UI_BOOT_WELCOME:
        draw_centered(19, FONT_LARGE, "Welcome", BSP_LCD_WHITE);
        draw_centered(42, FONT_LARGE, APP_VERSION_STR, BSP_LCD_GREEN);
        break;

    case UI_BOOT_SD_MEMORY:
        draw_centered(5, FONT_LARGE, "Memory", BSP_LCD_WHITE);
        snprintf(line, sizeof(line), "total: %" PRIu64 "GB",
                 bytes_to_gb(s_total_bytes));
        draw_centered(31, FONT_SMALL, line, BSP_LCD_GREEN);
        snprintf(line, sizeof(line), "free: %" PRIu64 "GB",
                 bytes_to_gb(s_free_bytes));
        draw_centered(49, FONT_SMALL, line, BSP_LCD_CYAN);
        break;

    case UI_BOOT_CONNECTING:
        fit_text(ssid, sizeof(ssid), s_ssid);
        draw_centered(15, FONT_SMALL, "Connecting to", BSP_LCD_WHITE);
        draw_centered(36, FONT_SMALL, ssid, BSP_LCD_GREEN);
        break;

    case UI_BOOT_CONFIG_INVALID:
        draw_centered(16, FONT_MEDIUM, "wifi.cfg invalid", BSP_LCD_RED);
        draw_centered(40, FONT_SMALL, "Fix and reboot", BSP_LCD_WHITE);
        break;

    case UI_BOOT_CONFIG_CREATED:
        draw_centered(8, FONT_SMALL, "wifi.cfg created", BSP_LCD_GREEN);
        draw_centered(29, FONT_SMALL, "Fill credentials", BSP_LCD_WHITE);
        draw_centered(50, FONT_SMALL, "and reboot", BSP_LCD_WHITE);
        break;

    case UI_BOOT_CONNECT_FAILED:
        fit_text(ssid, sizeof(ssid), s_ssid);
        draw_centered(2, FONT_SMALL, "Can't connect", BSP_LCD_RED);
        draw_centered(20, FONT_SMALL, ssid, BSP_LCD_WHITE);
        draw_centered(40, FONT_SMALL, "USB drive", BSP_LCD_BLUE);
        draw_centered(58, FONT_SMALL, "mode active", BSP_LCD_BLUE);
        break;

    case UI_MODE_USB:
    case UI_MODE_HTTP:
    case UI_SWITCHING:
        draw_wifi_mode(screen);
        break;

    case UI_ERROR_SD:
        draw_centered(18, FONT_LARGE, "SD Card", BSP_LCD_RED);
        draw_centered(43, FONT_LARGE, "required", BSP_LCD_WHITE);
        break;

    case UI_ERROR_GENERIC:
    default:
        draw_centered(18, FONT_LARGE, "Error", BSP_LCD_RED);
        draw_centered(43, FONT_SMALL, "Check serial log", BSP_LCD_WHITE);
        break;
    }
}

void ui_state_update_wifi(const char *ssid, const char *ip)
{
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid != NULL ? ssid : "");
    snprintf(s_ip, sizeof(s_ip), "%s", ip != NULL ? ip : "");
}

void ui_state_update_memory(uint64_t total_bytes, uint64_t free_bytes)
{
    s_total_bytes = total_bytes;
    s_free_bytes = free_bytes;
}

void ui_state_show(ui_screen_t screen)
{
    ESP_LOGI(TAG, "show %s ssid='%s' ip='%s' total=%" PRIu64 " free=%" PRIu64,
             ui_state_name(screen), s_ssid, s_ip, s_total_bytes, s_free_bytes);
    draw_screen(screen);
    ui_led_set_mode(screen);
}
