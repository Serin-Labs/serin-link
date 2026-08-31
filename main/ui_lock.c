/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_lock.h"

void lock_init(lock_state_t *l, bool enabled)
{
    l->enabled = enabled;
    l->window_open = false;
}
bool lock_engaged(const lock_state_t *l) { return l->enabled && !l->window_open; }
void lock_open_window(lock_state_t *l)   { if (l->enabled) l->window_open = true; }
void lock_note_idle(lock_state_t *l)     { l->window_open = false; }
void lock_disarm(lock_state_t *l)        { l->enabled = false; l->window_open = false; }
