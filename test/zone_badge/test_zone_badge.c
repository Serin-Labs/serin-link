/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "zone_badge.h"

static void test_checking_short_circuits(void) {
    assert(zone_badge_decide(true, true,  true,  true)  == UI_ZB_CHECKING);
    assert(zone_badge_decide(true, false, false, false) == UI_ZB_CHECKING);
}

static void test_fresh_cross_channel_is_online(void) {
    assert(zone_badge_decide(false, true, false, true) == UI_ZB_ONLINE);
}

static void test_fresh_same_channel_is_online(void) {
    assert(zone_badge_decide(false, true, false, false) == UI_ZB_ONLINE);
}

static void test_fault_wins_within_live(void) {
    assert(zone_badge_decide(false, true, true, false) == UI_ZB_FAULT);
    assert(zone_badge_decide(false, true, true, true)  == UI_ZB_FAULT);
}

static void test_not_fresh_cross_channel_is_stale(void) {
    assert(zone_badge_decide(false, false, false, true) == UI_ZB_STALE);
}

static void test_not_fresh_same_channel_is_offline(void) {
    assert(zone_badge_decide(false, false, false, false) == UI_ZB_OFFLINE);
}

static void test_fault_ignored_when_not_live(void) {
    assert(zone_badge_decide(false, false, true, false) == UI_ZB_OFFLINE);
    assert(zone_badge_decide(false, false, true, true)  == UI_ZB_STALE);
}

int main(void) {
    test_checking_short_circuits();
    test_fresh_cross_channel_is_online();
    test_fresh_same_channel_is_online();
    test_fault_wins_within_live();
    test_not_fresh_cross_channel_is_stale();
    test_not_fresh_same_channel_is_offline();
    test_fault_ignored_when_not_live();
    printf("zone_badge: all tests passed\n");
    return 0;
}
