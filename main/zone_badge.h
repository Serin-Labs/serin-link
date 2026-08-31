/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>

typedef enum { UI_ZB_OFFLINE, UI_ZB_STALE, UI_ZB_ONLINE, UI_ZB_FAULT,
               UI_ZB_CHECKING } ui_zone_badge_t;

static inline ui_zone_badge_t zone_badge_decide(bool checking, bool live,
                                                bool fault, bool cross_channel) {
    if (checking) return UI_ZB_CHECKING;
    if (live)     return fault ? UI_ZB_FAULT : UI_ZB_ONLINE;
    return cross_channel ? UI_ZB_STALE : UI_ZB_OFFLINE;
}
