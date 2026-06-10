// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_BOOT_WELCOME,
    UI_BOOT_SD_MEMORY,
    UI_BOOT_CONNECTING,
    UI_BOOT_CONFIG_INVALID,
    UI_BOOT_CONFIG_CREATED,
    UI_BOOT_CONNECT_FAILED,
    UI_MODE_USB,
    UI_MODE_HTTP,
    UI_SWITCHING,
    UI_ERROR_SD,
    UI_ERROR_GENERIC,
} ui_screen_t;

void ui_state_init(void);
void ui_state_show(ui_screen_t screen);
void ui_state_update_wifi(const char *ssid, const char *ip);
void ui_state_update_memory(uint64_t total_bytes, uint64_t free_bytes);
const char *ui_state_name(ui_screen_t screen);

#ifdef __cplusplus
}
#endif
