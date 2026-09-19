/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include "factory_reset.h"

int main(void)
{
    assert(reset_gesture_step(false, true, 20000) == RG_CANCELLED);
    assert(reset_gesture_step(true, true, 0)     == RG_WAITING);
    assert(reset_gesture_step(true, true, 9950)  == RG_WAITING);
    assert(reset_gesture_step(true, false, 5000) == RG_CANCELLED);
    assert(reset_gesture_step(true, true, 10000) == RG_FIRE);
    assert(reset_gesture_boot_armed(true, true));
    assert(!reset_gesture_boot_armed(true, false));
    assert(!reset_gesture_boot_armed(false, true));
    assert(!reset_gesture_boot_armed(false, false));
    puts("reset_gesture: OK");
    return 0;
}
