/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"

/* Call after lvgl_port_init(), before creating the UI. Keeps LVGL timers and
 * event handling active while replacing the port's periodic tick. */
esp_err_t display_runtime_start(void);

/* One permanent QSPI display. Call once with the LVGL mutex held, before its
 * first refresh. Owns color completion and, in blocking mode, the flush wait.
 * The polling baseline retains the same transfer-completion instrumentation. */
esp_err_t display_runtime_attach(lv_display_t *disp, esp_lcd_panel_io_handle_t io,
                                 bool blocking);
