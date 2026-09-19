/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
void factory_reset_execute(void);

typedef enum { RG_WAITING, RG_CANCELLED, RG_FIRE } reset_gesture_t;
reset_gesture_t reset_gesture_step(bool held_at_boot, bool held_now, int hold_ms);
/* A software restart (forget zone, OTA, panic) can land here with the knob
 * still held from the action that caused it; only a power-on arms the gesture. */
bool reset_gesture_boot_armed(bool power_on, bool held);

void factory_reset_boot_check(void);

void factory_reset_face_build(int pct);
