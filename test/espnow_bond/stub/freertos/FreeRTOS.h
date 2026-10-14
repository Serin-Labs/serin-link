/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
/* Host stand-in: no scheduler, so the test records lock use instead. */
#pragma once
#include <stdint.h>

typedef int      BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m)  ((void)(m))
