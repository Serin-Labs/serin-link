/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "touch_retry.h"

static touch_act_t fail_n(touch_retry_t *t, int n) {
    touch_act_t a = TOUCH_ACT_NONE;
    for (int i = 0; i < n; i++) a = touch_retry_note_fail(t);
    return a;
}

static void test_single_failures_are_ridden_out(void) {
    touch_retry_t t = {0};
    for (int i = 1; i < TOUCH_RETRY_FAIL_LIMIT; i++) {
        assert(touch_retry_note_fail(&t) == TOUCH_ACT_NONE);
        assert(!t.offline);
        assert(touch_retry_should_read(&t, 0));
    }
}

static void test_limit_buys_one_recovery(void) {
    touch_retry_t t = {0};
    assert(fail_n(&t, TOUCH_RETRY_FAIL_LIMIT) == TOUCH_ACT_RECOVER);
    assert(t.offline);
    assert(t.recoveries == 1);
}

static void test_consecutive_means_consecutive(void) {
    touch_retry_t t = {0};
    fail_n(&t, TOUCH_RETRY_FAIL_LIMIT - 1);
    touch_retry_note_ok(&t);
    assert(t.fails == 0);
    for (int i = 1; i < TOUCH_RETRY_FAIL_LIMIT; i++)
        assert(touch_retry_note_fail(&t) == TOUCH_ACT_NONE);
    assert(fail_n(&t, 1) == TOUCH_ACT_RECOVER);
}

static void test_offline_throttles_the_bus(void) {
    touch_retry_t t = {0};
    fail_n(&t, TOUCH_RETRY_FAIL_LIMIT);
    touch_retry_note_read(&t, 1000000);
    assert(!touch_retry_should_read(&t, 1000000));
    assert(!touch_retry_should_read(&t, 1000000 + TOUCH_RETRY_PROBE_US - 1));
    assert(touch_retry_should_read(&t, 1000000 + TOUCH_RETRY_PROBE_US));
}

static void test_online_never_throttles(void) {
    touch_retry_t t = {0};
    touch_retry_note_read(&t, 5000000);
    assert(touch_retry_should_read(&t, 5000000));
}

static void test_recovery_budget_is_bounded(void) {
    touch_retry_t t = {0};
    for (int i = 0; i < TOUCH_RETRY_MAX_RECOVERIES; i++)
        assert(fail_n(&t, TOUCH_RETRY_FAIL_LIMIT) == TOUCH_ACT_RECOVER);
    assert(t.recoveries == TOUCH_RETRY_MAX_RECOVERIES);
    for (int i = 0; i < 20; i++)
        assert(fail_n(&t, TOUCH_RETRY_FAIL_LIMIT) == TOUCH_ACT_NONE);
    assert(t.offline);
    assert(touch_retry_should_read(&t, (int64_t)1 << 40));
}

static void test_a_good_read_ends_the_episode(void) {
    touch_retry_t t = {0};
    for (int i = 0; i < TOUCH_RETRY_MAX_RECOVERIES; i++)
        fail_n(&t, TOUCH_RETRY_FAIL_LIMIT);
    assert(touch_retry_note_ok(&t));
    assert(!t.offline && t.fails == 0 && t.recoveries == 0);
    assert(!touch_retry_note_ok(&t));
    assert(fail_n(&t, TOUCH_RETRY_FAIL_LIMIT) == TOUCH_ACT_RECOVER);
}

static void test_retry_now_short_circuits_the_throttle(void) {
    touch_retry_t t = {0};
    fail_n(&t, TOUCH_RETRY_FAIL_LIMIT);
    touch_retry_note_read(&t, 9000000);
    assert(!touch_retry_should_read(&t, 9000000));
    touch_retry_retry_now(&t);
    assert(touch_retry_should_read(&t, 9000000));
}

int main(void) {
    test_single_failures_are_ridden_out();
    test_limit_buys_one_recovery();
    test_consecutive_means_consecutive();
    test_offline_throttles_the_bus();
    test_online_never_throttles();
    test_recovery_budget_is_bounded();
    test_a_good_read_ends_the_episode();
    test_retry_now_short_circuits_the_throttle();
    printf("touch_retry: all tests passed\n");
    return 0;
}
