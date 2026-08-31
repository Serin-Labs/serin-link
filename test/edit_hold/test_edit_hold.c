/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "edit_hold.h"

#define WIN 3000

static void test_inactive_hold_passes_echo_through(void) {
    edit_hold_t h = {0};
    assert(edit_hold_echo(&h, 42, 1000, WIN) == 42);
    assert(!h.on);
}

static void test_lagging_echo_is_suppressed(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(edit_hold_echo(&h, 21, 1500, WIN) == 22);
    assert(h.on);
}

static void test_matching_echo_confirms_and_clears(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(edit_hold_echo(&h, 22, 1500, WIN) == 22);
    assert(!h.on);
    assert(edit_hold_echo(&h, 25, 1600, WIN) == 25);
}

static void test_expiry_lets_unit_truth_win(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(edit_hold_echo(&h, 21, 1000 + WIN + 1, WIN) == 21);
    assert(!h.on);
}

static void test_window_boundary_is_exclusive(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(edit_hold_echo(&h, 21, 1000 + WIN, WIN) == 22);
    assert(h.on);
}

static void test_renote_extends_the_window(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    edit_hold_note(&h, 23, 1000 + WIN);
    assert(edit_hold_echo(&h, 21, 1000 + WIN + 100, WIN) == 23);
    assert(h.on);
}

static void test_drop_clears_the_hold(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    edit_hold_drop(&h);
    assert(edit_hold_echo(&h, 21, 1100, WIN) == 21);
}

static void test_ms_wrap_keeps_holding(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, UINT32_MAX - 100);
    assert(edit_hold_echo(&h, 21, 500, WIN) == 22);
    assert(h.on);
    assert(edit_hold_echo(&h, 21, WIN + 500, WIN) == 21);
    assert(!h.on);
}

static void test_pair_suppresses_until_both_match(void) {
    edit_hold2_t h = {0};
    edit_hold2_note(&h, 200, 240, 1000);
    int lo = 190, hi = 240;
    edit_hold2_echo(&h, &lo, &hi, 1500, WIN);
    assert(lo == 200 && hi == 240);
    assert(h.on);
}

static void test_pair_confirms_when_both_match(void) {
    edit_hold2_t h = {0};
    edit_hold2_note(&h, 200, 240, 1000);
    int lo = 200, hi = 240;
    edit_hold2_echo(&h, &lo, &hi, 1500, WIN);
    assert(lo == 200 && hi == 240);
    assert(!h.on);
}

static void test_pair_expiry_lets_unit_truth_win(void) {
    edit_hold2_t h = {0};
    edit_hold2_note(&h, 200, 240, 1000);
    int lo = 190, hi = 230;
    edit_hold2_echo(&h, &lo, &hi, 1000 + WIN + 1, WIN);
    assert(lo == 190 && hi == 230);
    assert(!h.on);
}

static void test_pair_drop_clears(void) {
    edit_hold2_t h = {0};
    edit_hold2_note(&h, 200, 240, 1000);
    edit_hold2_drop(&h);
    int lo = 190, hi = 230;
    edit_hold2_echo(&h, &lo, &hi, 1100, WIN);
    assert(lo == 190 && hi == 230);
}

#define DEBOUNCE 1000

static void test_tx_waits_for_the_settle_window(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(!edit_hold_tx_due(&h, -1000, 1000 + DEBOUNCE - 1, DEBOUNCE, false));
    assert(edit_hold_tx_due(&h, -1000, 1000 + DEBOUNCE, DEBOUNCE, false));
}

static void test_tx_renote_restarts_the_settle_clock(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    edit_hold_note(&h, 23, 1800);
    assert(!edit_hold_tx_due(&h, -1000, 1800 + DEBOUNCE - 1, DEBOUNCE, false));
    assert(edit_hold_tx_due(&h, -1000, 1800 + DEBOUNCE, DEBOUNCE, false));
}

static void test_tx_skips_already_sent_value(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(!edit_hold_tx_due(&h, 22, 1000 + DEBOUNCE, DEBOUNCE, false));
    assert(h.on);
}

static void test_tx_inactive_hold_never_due(void) {
    edit_hold_t h = {0};
    assert(!edit_hold_tx_due(&h, -1000, 99999, DEBOUNCE, false));
    assert(!edit_hold_tx_due(&h, -1000, 99999, DEBOUNCE, true));
}

static void test_tx_flush_overrides_the_clock(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(edit_hold_tx_due(&h, -1000, 1001, DEBOUNCE, true));
}

static void test_tx_flush_with_nothing_pending_is_quiet(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, 1000);
    assert(!edit_hold_tx_due(&h, 22, 1001, DEBOUNCE, true));
}

static void test_tx_ms_wrap_still_settles(void) {
    edit_hold_t h = {0};
    edit_hold_note(&h, 22, UINT32_MAX - 100);
    assert(!edit_hold_tx_due(&h, -1000, UINT32_MAX - 50, DEBOUNCE, false));
    assert(edit_hold_tx_due(&h, -1000, DEBOUNCE - 101, DEBOUNCE, false));
}

static void test_tx_pair_due_when_either_edge_moves(void) {
    edit_hold2_t h = {0};
    edit_hold2_note(&h, 200, 240, 1000);
    assert(!edit_hold2_tx_due(&h, 200, 240, 1000 + DEBOUNCE, DEBOUNCE, false));
    assert(edit_hold2_tx_due(&h, 200, 230, 1000 + DEBOUNCE, DEBOUNCE, false));
    assert(edit_hold2_tx_due(&h, 190, 240, 1000 + DEBOUNCE, DEBOUNCE, false));
    assert(!edit_hold2_tx_due(&h, 190, 230, 1000 + DEBOUNCE - 1, DEBOUNCE, false));
    assert(edit_hold2_tx_due(&h, 190, 230, 1001, DEBOUNCE, true));
}

static void test_no_edit_before_first_state(void) {
    assert(!edit_tx_allowed(1, false));
    assert(!edit_tx_allowed(3, false));
}

static void test_no_edit_with_no_bond(void) {
    assert(!edit_tx_allowed(0, false));
    assert(!edit_tx_allowed(0, true));
}

static void test_edit_allowed_once_synced(void) {
    assert(edit_tx_allowed(1, true));
    assert(edit_tx_allowed(3, true));
}

int main(void) {
    test_inactive_hold_passes_echo_through();
    test_lagging_echo_is_suppressed();
    test_matching_echo_confirms_and_clears();
    test_expiry_lets_unit_truth_win();
    test_window_boundary_is_exclusive();
    test_renote_extends_the_window();
    test_drop_clears_the_hold();
    test_ms_wrap_keeps_holding();
    test_pair_suppresses_until_both_match();
    test_pair_confirms_when_both_match();
    test_pair_expiry_lets_unit_truth_win();
    test_pair_drop_clears();
    test_tx_waits_for_the_settle_window();
    test_tx_renote_restarts_the_settle_clock();
    test_tx_skips_already_sent_value();
    test_tx_inactive_hold_never_due();
    test_tx_flush_overrides_the_clock();
    test_tx_flush_with_nothing_pending_is_quiet();
    test_tx_ms_wrap_still_settles();
    test_tx_pair_due_when_either_edge_moves();
    test_no_edit_before_first_state();
    test_no_edit_with_no_bond();
    test_edit_allowed_once_synced();
    printf("edit_hold: all tests passed\n");
    return 0;
}
