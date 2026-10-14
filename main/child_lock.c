/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "child_lock.h"

void child_lock_init(child_lock_t *l, bool enabled)
{
    l->enabled = enabled;
    l->window_open = false;
}
bool child_lock_engaged(const child_lock_t *l) { return l->enabled && !l->window_open; }
void child_lock_open_window(child_lock_t *l)   { if (l->enabled) l->window_open = true; }
void child_lock_note_idle(child_lock_t *l)     { l->window_open = false; }
void child_lock_disarm(child_lock_t *l)        { l->enabled = false; l->window_open = false; }

bool child_lock_gates(const child_lock_t *l, child_lock_face_t face)
{
    return child_lock_engaged(l) &&
           face != CHILD_LOCK_FACE_WIFI_OFF && face != CHILD_LOCK_FACE_UNPAIRED;
}
