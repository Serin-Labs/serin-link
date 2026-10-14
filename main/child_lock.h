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

/* The home face, as far as the lock is concerned. */
typedef enum {
    CHILD_LOCK_FACE_LIVE,
    CHILD_LOCK_FACE_LINK_OFF,   /* controller offline */
    CHILD_LOCK_FACE_CN105_OFF,  /* heat pump offline */
    CHILD_LOCK_FACE_REMIND,     /* an offline face dismissed to the dimmed home face */
    CHILD_LOCK_FACE_WIFI_OFF,   /* controller has no Wi-Fi network: the setup offer */
    CHILD_LOCK_FACE_UNPAIRED,   /* no bond, or a pairing run */
} child_lock_face_t;

/* True when an engaged lock refuses ordinary input on this face. The setup
 * faces stay open so a dial that was never set up can still be set up. */
bool child_lock_gates(const child_lock_t *l, child_lock_face_t face);
