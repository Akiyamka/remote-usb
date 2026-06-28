// SPDX-License-Identifier: MIT
//
// LCD UI state machine for the 160x80 ST7735 panel.

#include "ui_state.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "bsp_lcd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ui_led.h"

static const char *TAG = "ui_state";

#define APP_VERSION_STR  "v0.0.10"

static char s_ssid[33];
static char s_ip[16];
static uint64_t s_total_bytes;
static uint64_t s_free_bytes;
static bool s_lcd_ok;
static SemaphoreHandle_t s_mutex;
static ui_screen_t s_current_screen = UI_BOOT_WELCOME;

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
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "mutex allocation failed");
        }
    }

    esp_err_t ret = bsp_lcd_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_lcd_init failed: %s", esp_err_to_name(ret));
        s_lcd_ok = false;
        return;
    }

    s_lcd_ok = true;
    ESP_LOGI(TAG, "LCD UI initialised");
}

static void lock_state(void)
{
    if (s_mutex != NULL) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock_state(void)
{
    if (s_mutex != NULL) {
        xSemaphoreGive(s_mutex);
    }
}

static void fit_text_px(char *dst, size_t dst_size, const char *src,
                        font_size_t font, int16_t max_width)
{
    if (dst_size == 0) {
        return;
    }

    if (max_width <= 0) {
        dst[0] = '\0';
        return;
    }

    snprintf(dst, dst_size, "%s", src != NULL ? src : "");
    if (bsp_lcd_measure_text(font, dst) <= max_width) {
        return;
    }

    size_t len = strlen(dst);
    while (len > 0) {
        dst[len - 1] = '>';
        dst[len] = '\0';
        if (bsp_lcd_measure_text(font, dst) <= max_width) {
            return;
        }
        --len;
        dst[len] = '\0';
    }
}

static void format_capacity(char *dst, size_t dst_size, const char *label,
                            uint64_t bytes)
{
    const uint64_t mib = bytes / (1024ULL * 1024ULL);
    if (mib >= 1024) {
        snprintf(dst, dst_size, "%s:%" PRIu64 "GB", label,
                 (mib + 512ULL) / 1024ULL);
        return;
    }

    snprintf(dst, dst_size, "%s:%" PRIu64 "MB", label, mib);
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

static void draw_centered_fit(int16_t y, font_size_t font, const char *text,
                              uint16_t fg)
{
    char fitted[40];
    fit_text_px(fitted, sizeof(fitted), text, font, BSP_LCD_WIDTH);
    draw_centered(y, font, fitted, fg);
}

static void draw_wifi_mode(ui_screen_t screen)
{
    char ssid[33];
    char ip[16];
    fit_text_px(ssid, sizeof(ssid), s_ssid[0] != '\0' ? s_ssid : "USB drive",
                FONT_DELICATUS_16, BSP_LCD_WIDTH);

    draw_centered(2, FONT_DELICATUS_16, ssid, BSP_LCD_WHITE);
    if (s_ip[0] != '\0') {
        fit_text_px(ip, sizeof(ip), s_ip,
                    FONT_QUINQUEFIVE_10_DIGITS, BSP_LCD_WIDTH + 2);
        draw_centered(28, FONT_QUINQUEFIVE_10_DIGITS, ip, BSP_LCD_GREEN);
    } else {
        draw_centered_fit(28, FONT_DELICATUS_16, "No WiFi", BSP_LCD_GRAY);
    }

    const char *mode = "?";
    uint16_t mode_color = BSP_LCD_CYAN;
    if (screen == UI_MODE_HTTP) {
        mode = "Mode: HTTP";
    } else if (screen == UI_MODE_USB) {
        mode = "Mode: USB";
    } else if (screen == UI_SWITCHING) {
        mode = "Switching...";
        mode_color = BSP_LCD_YELLOW;
    }
    draw_centered_fit(58, FONT_DELICATUS_16, mode, mode_color);
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

    char line[48];
    char ssid[33];

    switch (screen) {
    case UI_BOOT_WELCOME:
        draw_centered_fit(4, FONT_DELICATUS_16, "Welcome", BSP_LCD_WHITE);
        draw_centered_fit(30, FONT_CAIROPIXEL_32,
                          APP_VERSION_STR, BSP_LCD_GREEN);
        break;

    case UI_BOOT_SD_MEMORY:
        draw_centered_fit(2, FONT_DELICATUS_16, "Memory", BSP_LCD_WHITE);
        format_capacity(line, sizeof(line), "total", s_total_bytes);
        draw_centered_fit(26, FONT_DELICATUS_16, line, BSP_LCD_GREEN);
        format_capacity(line, sizeof(line), "free", s_free_bytes);
        draw_centered_fit(48, FONT_DELICATUS_16, line, BSP_LCD_CYAN);
        break;

    case UI_BOOT_CONNECTING:
        fit_text_px(ssid, sizeof(ssid), s_ssid,
                    FONT_DELICATUS_16, BSP_LCD_WIDTH);
        draw_centered_fit(8, FONT_DELICATUS_16, "Connecting to", BSP_LCD_WHITE);
        draw_centered(32, FONT_DELICATUS_16, ssid, BSP_LCD_GREEN);
        break;

    case UI_BOOT_CONFIG_INVALID:
        draw_centered_fit(4, FONT_DELICATUS_16, "wifi.cfg", BSP_LCD_RED);
        draw_centered_fit(28, FONT_DELICATUS_16, "invalid", BSP_LCD_RED);
        draw_centered_fit(56, FONT_QUINQUEFIVE_5, "Fix and reboot", BSP_LCD_WHITE);
        break;

    case UI_BOOT_CONFIG_CREATED:
        draw_centered_fit(4, FONT_DELICATUS_16, "wifi.cfg", BSP_LCD_GREEN);
        draw_centered_fit(28, FONT_DELICATUS_16, "created", BSP_LCD_WHITE);
        draw_centered_fit(56, FONT_QUINQUEFIVE_5, "Fill and reboot", BSP_LCD_WHITE);
        break;

    case UI_BOOT_CONNECT_FAILED:
        fit_text_px(ssid, sizeof(ssid), s_ssid,
                    FONT_DELICATUS_16, BSP_LCD_WIDTH);
        draw_centered_fit(2, FONT_DELICATUS_16, "Can't connect", BSP_LCD_RED);
        snprintf(line, sizeof(line), "to %s", ssid);
        draw_centered_fit(28, FONT_DELICATUS_16, line, BSP_LCD_WHITE);
        draw_centered_fit(56, FONT_QUINQUEFIVE_5, "Fallback USB", BSP_LCD_BLUE);
        break;

    case UI_MODE_USB:
    case UI_MODE_HTTP:
    case UI_SWITCHING:
        draw_wifi_mode(screen);
        break;

    case UI_ERROR_SD:
        draw_centered_fit(10, FONT_DELICATUS_16, "SD Card", BSP_LCD_RED);
        draw_centered_fit(38, FONT_DELICATUS_16, "required", BSP_LCD_WHITE);
        break;

    case UI_ERROR_GENERIC:
    default:
        draw_centered_fit(4, FONT_CAIROPIXEL_32, "Error", BSP_LCD_RED);
        draw_centered_fit(36, FONT_DELICATUS_16, "Serial log", BSP_LCD_WHITE);
        break;
    }
}

void ui_state_update_wifi(const char *ssid, const char *ip)
{
    lock_state();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid != NULL ? ssid : "");
    snprintf(s_ip, sizeof(s_ip), "%s", ip != NULL ? ip : "");
    unlock_state();
}

void ui_state_update_memory(uint64_t total_bytes, uint64_t free_bytes)
{
    lock_state();
    s_total_bytes = total_bytes;
    s_free_bytes = free_bytes;
    unlock_state();
}

void ui_state_show(ui_screen_t screen)
{
    lock_state();
    s_current_screen = screen;
    ESP_LOGI(TAG, "show %s ssid='%s' ip='%s' total=%" PRIu64 " free=%" PRIu64,
             ui_state_name(screen), s_ssid, s_ip, s_total_bytes, s_free_bytes);
    draw_screen(screen);
    unlock_state();

    ui_led_set_mode(screen);
}
