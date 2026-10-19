/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "sdkconfig.h"

#if CONFIG_SERIN_SHT40

bool sensor_sht40_init(void);

bool sensor_sht40_present(void);

bool sensor_sht40_read(int16_t *temp_cc, uint16_t *hum_cc, uint32_t *age_ms);

int16_t sensor_sht40_offset_cc(void);
void    sensor_sht40_set_base_offset_cc(int16_t cc);

void sensor_sht40_set_idle_off(bool off);


#else

static inline bool sensor_sht40_init(void)    { return false; }
static inline bool sensor_sht40_present(void) { return false; }
static inline bool sensor_sht40_read(int16_t *t, uint16_t *h, uint32_t *a)
{ (void)t; (void)h; (void)a; return false; }
static inline int16_t sensor_sht40_offset_cc(void) { return 0; }
static inline void sensor_sht40_set_base_offset_cc(int16_t cc) { (void)cc; }
static inline void sensor_sht40_set_idle_off(bool off) { (void)off; }

#endif
