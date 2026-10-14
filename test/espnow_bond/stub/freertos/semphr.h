/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "freertos/FreeRTOS.h"

typedef struct { int taken; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf);
BaseType_t        xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait);
BaseType_t        xSemaphoreGive(SemaphoreHandle_t h);
