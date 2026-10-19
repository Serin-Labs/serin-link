// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
#include <stdbool.h>
void     sim_time_init(bool virtual_time);
uint32_t sim_time_ms(void);          /* lv_tick_set_cb target */
void     sim_time_wait(uint32_t ms); /* pumps lv_timer_handler; virtual time = instant wall-clock */
