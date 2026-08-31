/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "esp_lcd_touch.h"
#include "lvgl.h"

void bsp_touch_attach(lv_indev_t *indev, esp_lcd_touch_handle_t tp,
                      void (*recover)(void));
