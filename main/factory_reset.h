/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
void factory_reset_execute(void);

typedef enum { RG_WAITING, RG_CANCELLED, RG_FIRE } reset_gesture_t;
reset_gesture_t reset_gesture_step(bool held_at_boot, bool held_now, int hold_ms);

void factory_reset_boot_check(void);

void factory_reset_face_build(int pct);
