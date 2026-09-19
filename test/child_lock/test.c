/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include "child_lock.h"

int main(void)
{
    child_lock_t l;

    child_lock_init(&l, false);
    assert(!child_lock_engaged(&l));

    child_lock_init(&l, true);
    assert(child_lock_engaged(&l));

    child_lock_open_window(&l);
    assert(!child_lock_engaged(&l));
    child_lock_note_idle(&l);
    assert(child_lock_engaged(&l));

    child_lock_init(&l, false);
    child_lock_open_window(&l);
    assert(!l.window_open);
    child_lock_init(&l, true);
    child_lock_open_window(&l);
    child_lock_disarm(&l);
    assert(!child_lock_engaged(&l) && !l.enabled && !l.window_open);

    puts("child_lock: OK");
    return 0;
}
