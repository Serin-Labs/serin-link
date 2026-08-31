/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>

typedef struct {
    bool enabled;
    bool window_open;
} lock_state_t;

void lock_init(lock_state_t *l, bool enabled);
bool lock_engaged(const lock_state_t *l);
void lock_open_window(lock_state_t *l);
void lock_note_idle(lock_state_t *l);
void lock_disarm(lock_state_t *l);
