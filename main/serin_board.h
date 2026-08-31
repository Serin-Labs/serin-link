/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "sdkconfig.h"

#if CONFIG_SERIN_BOARD_VIEWE_15
#define SERIN_DISP 466
#else
#define SERIN_DISP 480
#endif

#if CONFIG_SERIN_BOARD_VIEWE_15
#define SERIN_MODEL_NAME "Serin Link 1.5\""
#else
#define SERIN_MODEL_NAME "Serin Link 2.1\""
#endif
