/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL } gpio_int_type_t;
typedef void (*gpio_isr_t)(void *);
