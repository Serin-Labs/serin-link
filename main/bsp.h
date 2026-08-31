/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#if defined(__has_include)
#  if __has_include("sdkconfig.h")
#    include "sdkconfig.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

lv_display_t *bsp_display_start(void);
void          bsp_backlight_set(int pct);
void          bsp_display_sleep(bool sleep);

void          bsp_register_console(void);

#if CONFIG_SERIN_BOARD_VIEWE_15
void          bsp_display_deep_off(bool off);
#endif

void bsp_input_start(void (*on_encoder)(int delta), void (*on_button)(void),
                     void (*on_button_hold)(void),
                     void (*on_press_down)(void), void (*on_press_up)(void));

bool bsp_button_pressed(void);

#ifdef __cplusplus
}
#endif
