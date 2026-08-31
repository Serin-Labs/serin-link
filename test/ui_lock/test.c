/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include "ui_lock.h"

int main(void)
{
    lock_state_t l;

    lock_init(&l, false);
    assert(!lock_engaged(&l));

    lock_init(&l, true);
    assert(lock_engaged(&l));

    lock_open_window(&l);
    assert(!lock_engaged(&l));
    lock_note_idle(&l);
    assert(lock_engaged(&l));

    lock_init(&l, false);
    lock_open_window(&l);
    assert(!l.window_open);
    lock_init(&l, true);
    lock_open_window(&l);
    lock_disarm(&l);
    assert(!lock_engaged(&l) && !l.enabled && !l.window_open);

    puts("ui_lock: OK");
    return 0;
}
