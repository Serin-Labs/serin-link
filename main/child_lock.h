/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>

typedef struct {
    bool enabled;
    bool window_open;
} child_lock_t;

void child_lock_init(child_lock_t *l, bool enabled);
bool child_lock_engaged(const child_lock_t *l);
void child_lock_open_window(child_lock_t *l);
void child_lock_note_idle(child_lock_t *l);
void child_lock_disarm(child_lock_t *l);
