/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "espnow_chscan.h"

static void test_due_needs_misses_and_no_ack(void) {
    assert(!espnow_chscan_due(0, false));
    assert(!espnow_chscan_due(ESPNOW_CHSCAN_MISS_N - 1, false));
    assert(espnow_chscan_due(ESPNOW_CHSCAN_MISS_N, false));
    assert(espnow_chscan_due(255, false));
}

static void test_online_zone_never_scanned(void) {
    assert(!espnow_chscan_due(ESPNOW_CHSCAN_MISS_N, true));
    assert(!espnow_chscan_due(255, true));
}

static void test_field_log_2026_07_16(void) {
    uint8_t sib[] = { 11 };
    assert(espnow_chscan_channel(0, 11, 11, sib, 1) == 1);
}

static void test_popular_first_no_siblings(void) {
    static const uint8_t want[] = { 1, 6, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13 };
    for (int i = 0; i < (int)sizeof want; i++)
        assert(espnow_chscan_channel(i, 11, 11, NULL, 0) == want[i]);
    assert(espnow_chscan_channel((int)sizeof want, 11, 11, NULL, 0) == 0);
}

static void test_sibling_channel_leads(void) {
    uint8_t sib[] = { 3 };
    assert(espnow_chscan_channel(0, 11, 11, sib, 1) == 3);
    assert(espnow_chscan_channel(1, 11, 11, sib, 1) == 1);
    assert(espnow_chscan_channel(2, 11, 11, sib, 1) == 6);
}

static void test_sibling_dups_and_home_skipped(void) {
    uint8_t sib[] = { 6, 6, 11, 3 };
    assert(espnow_chscan_channel(0, 11, 11, sib, 4) == 6);
    assert(espnow_chscan_channel(1, 11, 11, sib, 4) == 3);
    assert(espnow_chscan_channel(2, 11, 11, sib, 4) == 1);
}

static void test_own_channel_skipped(void) {
    assert(espnow_chscan_channel(0, 6, 1, NULL, 0) == 11);
    for (int i = 0; ; i++) {
        uint8_t ch = espnow_chscan_channel(i, 6, 1, NULL, 0);
        if (!ch) break;
        assert(ch != 1 && ch != 6);
    }
}

static void test_never_locked_zone_scans_everything(void) {
    int n = 0;
    for (int i = 0; ; i++) {
        uint8_t ch = espnow_chscan_channel(i, 11, 0, NULL, 0);
        if (!ch) break;
        assert(ch >= 1 && ch <= 13 && ch != 11);
        n++;
    }
    assert(n == 12);
}

static void test_round_terminates(void) {
    uint8_t sib[] = { 1, 6, 11, 13, 2 };
    int n = 0;
    for (int i = 0; espnow_chscan_channel(i, 1, 0, sib, 5); i++) n++;
    assert(n > 0 && n <= 13);
}

static void test_candidates_unique_and_valid(void) {
    uint8_t sib[] = { 4, 9 };
    uint16_t seen = 0;
    for (int i = 0; ; i++) {
        uint8_t ch = espnow_chscan_channel(i, 6, 4, sib, 2);
        if (!ch) break;
        assert(ch >= 1 && ch <= 13);
        assert(ch != 6 && ch != 4);
        assert(!(seen & (1u << ch)));
        seen |= 1u << ch;
    }
}

static void test_relock_tries_remembered_first(void) {
    for (int i = 0; i < ESPNOW_RELOCK_TRIES; i++)
        assert(espnow_relock_channel(i, 11) == 11);
    assert(espnow_relock_channel(ESPNOW_RELOCK_TRIES, 11) != 11);
}

static void test_relock_walks_1_to_13_skipping_remembered(void) {
    const uint8_t rem = 11;
    uint8_t want = 1;
    for (int i = ESPNOW_RELOCK_TRIES; ; i++) {
        uint8_t ch = espnow_relock_channel(i, rem);
        if (!ch) break;
        if (want == rem) want++;
        assert(ch == want);
        want++;
    }
    assert(want == 14);
}

static void test_relock_no_remembered_channel(void) {
    for (uint8_t ch = 1; ch <= 13; ch++)
        assert(espnow_relock_channel(ch - 1, 0) == ch);
    assert(espnow_relock_channel(13, 0) == 0);
}

static void test_relock_round_terminates_and_is_unique(void) {
    for (uint8_t rem = 0; rem <= 13; rem++) {
        uint16_t seen = 0;
        int n = 0;
        for (int i = 0; ; i++) {
            uint8_t ch = espnow_relock_channel(i, rem);
            if (!ch) break;
            assert(ch >= 1 && ch <= 13);
            if (ch != rem) assert(!(seen & (1u << ch)));
            seen |= 1u << ch;
            assert(++n <= 13 + ESPNOW_RELOCK_TRIES);
        }
        assert(n == (rem >= 1 && rem <= 13 ? 13 + ESPNOW_RELOCK_TRIES - 1 : 13));
    }
}

static void test_relock_rejects_negative_step(void) {
    assert(espnow_relock_channel(-1, 6) == 0);
}

static void test_relock_rest_stays_fast_for_the_first_rounds(void) {
    assert(espnow_relock_rest_ms(0) == ESPNOW_RELOCK_REST_MS);
    assert(espnow_relock_rest_ms(1) == ESPNOW_RELOCK_REST_MS);
    assert(espnow_relock_rest_ms(ESPNOW_RELOCK_FAST_ROUNDS) == ESPNOW_RELOCK_REST_MS);
}

static void test_relock_rest_doubles_past_the_fast_rounds(void) {
    assert(espnow_relock_rest_ms(ESPNOW_RELOCK_FAST_ROUNDS + 1) == ESPNOW_RELOCK_REST_MS * 2);
    assert(espnow_relock_rest_ms(ESPNOW_RELOCK_FAST_ROUNDS + 2) == ESPNOW_RELOCK_REST_MS * 4);
    assert(espnow_relock_rest_ms(ESPNOW_RELOCK_FAST_ROUNDS + 3) == ESPNOW_RELOCK_REST_MS * 8);
}

static void test_relock_rest_caps_and_never_overflows(void) {
    for (int r = 0; r <= 255; r++) {
        uint32_t ms = espnow_relock_rest_ms((uint8_t)r);
        assert(ms >= ESPNOW_RELOCK_REST_MS);
        assert(ms <= ESPNOW_RELOCK_REST_MAX_MS);
    }
    assert(espnow_relock_rest_ms(255) == ESPNOW_RELOCK_REST_MAX_MS);
}

static void test_relock_rest_is_monotonic(void) {
    for (int r = 1; r <= 255; r++)
        assert(espnow_relock_rest_ms((uint8_t)r) >= espnow_relock_rest_ms((uint8_t)(r - 1)));
}

static void test_relock_rest_reaches_the_cap(void) {
    int first = -1;
    for (int r = 0; r <= 255 && first < 0; r++)
        if (espnow_relock_rest_ms((uint8_t)r) == ESPNOW_RELOCK_REST_MAX_MS) first = r;
    assert(first > ESPNOW_RELOCK_FAST_ROUNDS);
    assert(first <= 16);
}

int main(void) {
    test_due_needs_misses_and_no_ack();
    test_online_zone_never_scanned();
    test_field_log_2026_07_16();
    test_popular_first_no_siblings();
    test_sibling_channel_leads();
    test_sibling_dups_and_home_skipped();
    test_own_channel_skipped();
    test_never_locked_zone_scans_everything();
    test_round_terminates();
    test_candidates_unique_and_valid();
    test_relock_tries_remembered_first();
    test_relock_walks_1_to_13_skipping_remembered();
    test_relock_no_remembered_channel();
    test_relock_round_terminates_and_is_unique();
    test_relock_rejects_negative_step();
    test_relock_rest_stays_fast_for_the_first_rounds();
    test_relock_rest_doubles_past_the_fast_rounds();
    test_relock_rest_caps_and_never_overflows();
    test_relock_rest_is_monotonic();
    test_relock_rest_reaches_the_cap();
    printf("espnow_chscan: all tests passed\n");
    return 0;
}
