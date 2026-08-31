/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#if CONFIG_SERIN_THERMAL_CAL

void thermal_cal_start(void);

void thermal_cal_note_input(void);

void thermal_cal_dump(void);

bool thermal_cal_active(void);

#else

static inline void thermal_cal_start(void)      {}
static inline void thermal_cal_note_input(void) {}
static inline void thermal_cal_dump(void)       {}
static inline bool thermal_cal_active(void)     { return false; }

#endif
