// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);   /* µs since boot (sim: virtual/SDL clock) */
