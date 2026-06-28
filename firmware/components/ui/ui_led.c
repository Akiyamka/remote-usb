// SPDX-License-Identifier: MIT
//
// UI LED state machine for the on-board APA102.

#include "ui_led.h"

#include <stdbool.h>
#include <stdint.h>

#include "bsp_led.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ui_led";

#define UI_LED_TASK_STACK     4096
#define UI_LED_TASK_PRIORITY  2
#define UI_LED_TASK_CORE      1
#define UI_LED_TICK_MS        100
#define UI_LED_BLINK_MS       500
#define UI_LED_BRIGHTNESS     8

typedef enum {
    UI_LED_OFF,
    UI_LED_SOLID_BLUE,
    UI_LED_SOLID_GREEN,
    UI_LED_SOLID_YELLOW,
    UI_LED_BLINK_RED,
    UI_LED_BLINK_WHITE,
    UI_LED_BLINK_GREEN,
    UI_LED_PATTERN_COUNT,
} ui_led_pattern_t;

static bool s_led_ok;
static SemaphoreHandle_t s_mutex;
static TaskHandle_t s_task;
static ui_led_pattern_t s_pattern = UI_LED_OFF;

static bool pattern_is_blink(ui_led_pattern_t pattern)
{
    return pattern == UI_LED_BLINK_RED || pattern == UI_LED_BLINK_WHITE ||
        pattern == UI_LED_BLINK_GREEN;
}

static void pattern_rgb(ui_led_pattern_t pattern, uint8_t *r, uint8_t *g,
                        uint8_t *b)
{
    *r = 0;
    *g = 0;
    *b = 0;

    switch (pattern) {
    case UI_LED_SOLID_BLUE:
        *b = 255;
        break;
    case UI_LED_SOLID_GREEN:
    case UI_LED_BLINK_GREEN:
        *g = 255;
        break;
    case UI_LED_SOLID_YELLOW:
        *r = 255;
        *g = 255;
        break;
    case UI_LED_BLINK_RED:
        *r = 255;
        break;
    case UI_LED_BLINK_WHITE:
        *r = 255;
        *g = 255;
        *b = 255;
        break;
    case UI_LED_OFF:
    default:
        break;
    }
}

static esp_err_t apply_pattern(ui_led_pattern_t pattern, bool blink_on)
{
    if (pattern == UI_LED_OFF || !blink_on) {
        return bsp_led_off();
    }

    uint8_t r;
    uint8_t g;
    uint8_t b;
    pattern_rgb(pattern, &r, &g, &b);
    return bsp_led_set_rgb_brightness(r, g, b, UI_LED_BRIGHTNESS);
}

static ui_led_pattern_t get_pattern(void)
{
    ui_led_pattern_t pattern = s_pattern;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        pattern = s_pattern;
        xSemaphoreGive(s_mutex);
    }
    return pattern;
}

static void set_pattern(ui_led_pattern_t pattern)
{
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        s_pattern = pattern;
        xSemaphoreGive(s_mutex);
    } else {
        s_pattern = pattern;
    }

    if (s_led_ok && s_task == NULL) {
        (void)apply_pattern(pattern, true);
    }
}

static ui_led_pattern_t pattern_for_state(ui_screen_t state)
{
    switch (state) {
    case UI_BOOT_WELCOME:
    case UI_BOOT_SD_MEMORY:
        return UI_LED_OFF;

    case UI_BOOT_CONNECTING:
        return UI_LED_BLINK_WHITE;

    case UI_BOOT_CONFIG_CREATED:
        return UI_LED_BLINK_GREEN;

    case UI_MODE_HTTP:
        return UI_LED_SOLID_GREEN;

    case UI_MODE_USB:
        return UI_LED_SOLID_BLUE;

    case UI_SWITCHING:
        return UI_LED_SOLID_YELLOW;

    case UI_BOOT_CONFIG_INVALID:
    case UI_BOOT_CONNECT_FAILED:
    case UI_ERROR_SD:
    case UI_ERROR_GENERIC:
    default:
        return UI_LED_BLINK_RED;
    }
}

static void ui_led_task(void *arg)
{
    (void)arg;

    ui_led_pattern_t applied_pattern = UI_LED_PATTERN_COUNT;
    bool applied_blink_on = false;
    uint32_t elapsed_ms = 0;

    while (true) {
        const ui_led_pattern_t pattern = get_pattern();
        bool blink_on = true;
        if (pattern_is_blink(pattern)) {
            blink_on = ((elapsed_ms / UI_LED_BLINK_MS) & 1U) == 0;
        }

        if (pattern != applied_pattern || blink_on != applied_blink_on) {
            const esp_err_t ret = apply_pattern(pattern, blink_on);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "apply pattern %d failed: %s", pattern,
                         esp_err_to_name(ret));
            }
            applied_pattern = pattern;
            applied_blink_on = blink_on;
        }

        vTaskDelay(pdMS_TO_TICKS(UI_LED_TICK_MS));
        elapsed_ms += UI_LED_TICK_MS;
    }
}

void ui_led_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == NULL) {
            ESP_LOGE(TAG, "mutex allocation failed");
            return;
        }
    }

    esp_err_t ret = bsp_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_led_init failed: %s", esp_err_to_name(ret));
        s_led_ok = false;
        return;
    }

    s_led_ok = true;
    ESP_LOGI(TAG, "APA102 UI initialised");

    if (s_task == NULL) {
        const BaseType_t task_ok = xTaskCreatePinnedToCore(
            ui_led_task, "ui_led", UI_LED_TASK_STACK, NULL,
            UI_LED_TASK_PRIORITY, &s_task, UI_LED_TASK_CORE);
        if (task_ok != pdPASS) {
            ESP_LOGE(TAG, "task creation failed");
            s_task = NULL;
        }
    }
}

void ui_led_set_mode(ui_screen_t state)
{
    ESP_LOGI(TAG, "mode %s", ui_state_name(state));
    set_pattern(pattern_for_state(state));
}

void ui_led_set_special_yellow(void)
{
    ESP_LOGI(TAG, "special yellow");
    set_pattern(UI_LED_SOLID_YELLOW);
}
