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
/* Called by the QSPI color-completion ISR, before releasing the draw buffer. */
void perf_stats_transfer_done(void);
#else
static inline void perf_stats_init(lv_display_t *disp) { (void)disp; }
static inline void perf_stats_transfer_done(void) {}
#endif

#ifdef __cplusplus
}
#endif
