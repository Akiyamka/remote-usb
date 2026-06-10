// SPDX-License-Identifier: MIT
//
// Phase 6 minimal LED control. Real blink patterns arrive in phase 10.

#include "ui_led.h"

#include <stdbool.h>

#include "bsp_led.h"
#include "esp_log.h"

static const char *TAG = "ui_led";
static bool s_led_ok;

void ui_led_init(void)
{
    esp_err_t ret = bsp_led_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "bsp_led_init failed: %s", esp_err_to_name(ret));
        s_led_ok = false;
        return;
    }

    s_led_ok = true;
    ESP_LOGI(TAG, "APA102 UI initialised");
}

void ui_led_set_mode(ui_screen_t state)
{
    ESP_LOGI(TAG, "mode %s", ui_state_name(state));
    if (!s_led_ok) {
        return;
    }

    switch (state) {
    case UI_BOOT_WELCOME:
        (void)bsp_led_off();
        break;

    case UI_BOOT_CONNECTING:
        (void)bsp_led_set_rgb(255, 255, 255);
        break;

    case UI_BOOT_CONFIG_CREATED:
    case UI_MODE_HTTP:
        (void)bsp_led_set_rgb(0, 255, 0);
        break;

    case UI_MODE_USB:
        (void)bsp_led_set_rgb(0, 0, 255);
        break;

    case UI_SWITCHING:
        (void)bsp_led_set_rgb(255, 255, 0);
        break;

    case UI_BOOT_CONFIG_INVALID:
    case UI_BOOT_CONNECT_FAILED:
    case UI_ERROR_SD:
    case UI_ERROR_GENERIC:
        (void)bsp_led_set_rgb(255, 0, 0);
        break;

    case UI_BOOT_SD_MEMORY:
    default:
        (void)bsp_led_off();
        break;
    }
}

void ui_led_set_special_yellow(void)
{
    ESP_LOGI(TAG, "special yellow");
    if (s_led_ok) {
        (void)bsp_led_set_rgb(255, 255, 0);
    }
}
