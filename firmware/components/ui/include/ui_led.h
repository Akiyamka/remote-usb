// SPDX-License-Identifier: MIT

#pragma once

#include "ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_led_init(void);
void ui_led_set_mode(ui_screen_t state);
void ui_led_set_special_yellow(void);

#ifdef __cplusplus
}
#endif
