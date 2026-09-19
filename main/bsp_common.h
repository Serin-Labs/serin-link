/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 *
 * bsp_common.h — the handful of things both board BSPs do the same way.
 *
 * Only board-independent mechanics belong here. Pins, knob direction, backlight
 * polarity and code placement differ between the 1.5" and the 2.1" on purpose
 * and stay in their own bsp_viewe*.c — the button pin is passed in as an
 * argument for exactly that reason.
 */
#pragma once
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"

/* Configure the knob-press GPIO once, on first use. Both boards wire the press
 * to a pin that idles high through an internal pull-up, so a press reads 0. */
static inline void bsp_btn_gpio_ensure(int pin)
{
    static bool ready;
    if (ready) return;
    gpio_config_t btn = { .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
        .pin_bit_mask = 1ULL << pin };
    ESP_ERROR_CHECK(gpio_config(&btn));
    ready = true;
}

static inline bool bsp_btn_is_pressed(int pin)
{
    bsp_btn_gpio_ensure(pin);
    return gpio_get_level(pin) == 0;
}

/* Input arrives on a BSP task, so every hop into the UI has to hold the LVGL
 * port mutex. A NULL callback means the app never registered one — skip it
 * rather than take the lock. lvgl_port_lock(0) waits forever, which is what an
 * input event wants: dropping a click because a redraw was in flight would feel
 * like a dead button. */
static inline void bsp_ui_call(void (*fn)(void))
{
    if (fn && lvgl_port_lock(0)) { fn(); lvgl_port_unlock(); }
}

static inline void bsp_ui_call_i(void (*fn)(int), int arg)
{
    if (fn && lvgl_port_lock(0)) { fn(arg); lvgl_port_unlock(); }
}
