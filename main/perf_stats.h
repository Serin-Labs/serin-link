/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "sdkconfig.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_SERIN_PERF_STATS
void perf_stats_init(lv_display_t *disp);
#else
static inline void perf_stats_init(lv_display_t *disp) { (void)disp; }
#endif

#ifdef __cplusplus
}
#endif
