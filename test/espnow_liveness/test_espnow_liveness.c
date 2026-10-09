/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "espnow_liveness.h"

static void test_not_synced_never_stale(void) {
    assert(!espnow_rx_stale(1000000, 0, false));
    assert(!espnow_rx_stale(1000000, 1, false));
}

static void test_fresh_rx_not_stale(void) {
    uint32_t now = 500000;
    assert(!espnow_rx_stale(now, now, true));
    assert(!espnow_rx_stale(now, now - 10000, true));
    assert(!espnow_rx_stale(now, now - (ESPNOW_RX_STALE_MS - 1), true));
}

static void test_silent_app_is_stale(void) {
    uint32_t now = 500000;
    assert(espnow_rx_stale(now, now - ESPNOW_RX_STALE_MS, true));
    assert(espnow_rx_stale(now, now - 600000 + 100000, true));
}

static void test_unpair_scenario(void) {
    uint32_t now = 3600000;
    uint32_t last_rx = now - 600000;
    assert(espnow_rx_stale(now, last_rx, true));
}

static void test_u32_wraparound(void) {
    uint32_t last_rx = UINT32_MAX - 1000;
    uint32_t now = 5000;
    assert(!espnow_rx_stale(now, last_rx, true));
    now = last_rx + ESPNOW_RX_STALE_MS;
    assert(espnow_rx_stale(now, last_rx, true));
}

static void test_threshold_covers_missed_heartbeats(void) {
    assert(ESPNOW_RX_STALE_MS > 2 * 10000);
    assert(ESPNOW_RX_STALE_MS <= 30000);
}

static void test_unsynced_zone_wants_state(void) {
    assert(espnow_want_state(1000000, 0, false));
    assert(espnow_want_state(1000000, 999999, false));
}

static void test_fresh_zone_does_not_pull(void) {
    uint32_t now = 500000;
    assert(!espnow_want_state(now, now, true));
    assert(!espnow_want_state(now, now - 10000, true));
    assert(!espnow_want_state(now, now - (ESPNOW_RX_STALE_MS - 1), true));
}

static void test_stale_zone_pulls_to_refresh(void) {
    uint32_t now = 500000;
    assert(espnow_want_state(now, now - ESPNOW_RX_STALE_MS, true));
}

static void test_never_acked_is_not_fresh(void) {
    assert(!espnow_ack_fresh(0, 0, ESPNOW_ACK_WIN_ACTIVE_MS));
    assert(!espnow_ack_fresh(500000, 0, ESPNOW_ACK_WIN_BG_MS));
}

static void test_window_matches_cadence(void) {
    assert(espnow_ack_window(true)  == ESPNOW_ACK_WIN_ACTIVE_MS);
    assert(espnow_ack_window(false) == ESPNOW_ACK_WIN_BG_MS);
    assert(ESPNOW_ACK_WIN_BG_MS > 2 * ESPNOW_ACK_WIN_ACTIVE_MS);
}

static void test_fresh_ack_within_its_window(void) {
    uint32_t now = 500000;
    assert(espnow_ack_fresh(now, now, ESPNOW_ACK_WIN_ACTIVE_MS));
    assert(espnow_ack_fresh(now, now - 1000, ESPNOW_ACK_WIN_ACTIVE_MS));
    assert(!espnow_ack_fresh(now, now - ESPNOW_ACK_WIN_ACTIVE_MS, ESPNOW_ACK_WIN_ACTIVE_MS));
}

static void test_demoted_zone_cannot_flip_online(void) {
    uint32_t now = 114492, last_ack = now - 9134;
    uint32_t win = espnow_ack_window(true);
    assert(!espnow_ack_fresh(now, last_ack, win));
}

static void test_promoted_zone_stays_honest_for_the_bg_window(void) {
    uint32_t now = 114492, last_ack = now - 9134;
    uint32_t win = espnow_ack_window(false);
    assert(espnow_ack_fresh(now, last_ack, win));
}

static void test_promoted_zone_evidence_must_not_gate_the_sweep(void) {
    uint32_t now = 256882, last_ack = now - 15257;
    assert( espnow_ack_fresh(now, last_ack, ESPNOW_ACK_WIN_BG_MS));
    assert(!espnow_ack_fresh(now, last_ack, ESPNOW_ACK_WIN_ACTIVE_MS));
}

static void test_ack_u32_wraparound(void) {
    uint32_t last_ack = UINT32_MAX - 1000;
    uint32_t now = 500;
    assert(espnow_ack_fresh(now, last_ack, ESPNOW_ACK_WIN_ACTIVE_MS));
    now = last_ack + ESPNOW_ACK_WIN_ACTIVE_MS;
    assert(!espnow_ack_fresh(now, last_ack, ESPNOW_ACK_WIN_ACTIVE_MS));
}

static void test_never_probed_is_checking(void) {
    assert(espnow_zone_checking(1000000, 0, 0, false));
}

static void test_recently_probed_never_heard_is_checking(void) {
    uint32_t now = 500000;
    assert(espnow_zone_checking(now, now, 0, false));
    assert(espnow_zone_checking(now, now - (ESPNOW_CHECK_GRACE_MS - 1), 0, false));
}

static void test_grace_expired_is_not_checking(void) {
    uint32_t now = 500000;
    assert(!espnow_zone_checking(now, now - ESPNOW_CHECK_GRACE_MS, 0, false));
    assert(!espnow_zone_checking(now, now - 600000, 0, false));
}

static void test_any_ack_ends_checking(void) {
    uint32_t now = 500000;
    assert(!espnow_zone_checking(now, now, now, false));
    assert(!espnow_zone_checking(now, now, now - 999999, false));
}

static void test_synced_zone_is_not_checking(void) {
    assert(!espnow_zone_checking(500000, 500000, 0, true));
}

static void test_checking_u32_wraparound(void) {
    uint32_t first = UINT32_MAX - 1000;
    uint32_t now = 500;
    assert(espnow_zone_checking(now, first, 0, false));
    now = first + ESPNOW_CHECK_GRACE_MS;
    assert(!espnow_zone_checking(now, first, 0, false));
}

static void test_check_grace_is_a_sane_window(void) {
    assert(ESPNOW_CHECK_GRACE_MS >= 2000);
    assert(ESPNOW_CHECK_GRACE_MS <= 8000);
}

static void test_lone_ack_is_not_corroborated(void) {
    assert(!espnow_ack_corroborated(500000, 0, 0, 7));
}

static void test_second_ack_same_channel_corroborates(void) {
    uint32_t now = 500000;
    assert(espnow_ack_corroborated(now, now - 20, 7, 7));
    assert(espnow_ack_corroborated(now, now - 1000, 11, 11));
    assert(espnow_ack_corroborated(now, now - 4000, 11, 11));
}

static void test_second_ack_other_channel_does_not(void) {
    uint32_t now = 500000;
    assert(!espnow_ack_corroborated(now, now - 20, 3, 7));
}

static void test_stale_prior_ack_does_not_corroborate(void) {
    uint32_t now = 500000;
    assert(!espnow_ack_corroborated(now, now - ESPNOW_ACK_CORROB_MS, 7, 7));
    assert(!espnow_ack_corroborated(now, now - 600000, 7, 7));
}

static void test_corrob_window_covers_the_bg_cadence(void) {
    assert(ESPNOW_ACK_CORROB_MS > 4000);
    assert(ESPNOW_ACK_CORROB_MS <= 8000);
}

static void test_corrob_u32_wraparound(void) {
    uint32_t prev = UINT32_MAX - 1000;
    uint32_t now = 500;
    assert(espnow_ack_corroborated(now, prev, 7, 7));
    now = prev + ESPNOW_ACK_CORROB_MS;
    assert(!espnow_ack_corroborated(now, prev, 7, 7));
}

static void test_switch_warm_needs_all_three(void) {
    assert(espnow_switch_warm(true, true, false));
}

static void test_switch_cold_when_unsynced(void) {
    assert(!espnow_switch_warm(false, true, false));
}

static void test_switch_cold_when_ack_stale(void) {
    assert(!espnow_switch_warm(true, false, false));
}

static void test_switch_cold_when_rx_stale(void) {
    assert(!espnow_switch_warm(true, true, true));
}

static void test_fresh_controller_reply_survives_missed_radio_acks(void) {
    /* Observed: OFFLINE with ACK age 4191 ms but RX age only 191 ms. */
    const uint32_t now = 82538;
    assert(espnow_link_fresh(now, now - 4191, ESPNOW_ACK_WIN_ACTIVE_MS,
                            now - 191));
    assert(!espnow_ack_fresh(now, now - 4191, ESPNOW_ACK_WIN_ACTIVE_MS));
}

static void test_reply_evidence_expires_without_hiding_silence(void) {
    const uint32_t now = 500000;
    assert(espnow_link_fresh(now, 0, ESPNOW_ACK_WIN_ACTIVE_MS,
                            now - ESPNOW_ACK_WIN_ACTIVE_MS + 1));
    assert(!espnow_link_fresh(now, 0, ESPNOW_ACK_WIN_ACTIVE_MS,
                             now - ESPNOW_ACK_WIN_ACTIVE_MS));
    assert(!espnow_link_fresh(now, now - 10000, ESPNOW_ACK_WIN_ACTIVE_MS, 0));
    assert(!espnow_link_fresh(0, 0, ESPNOW_ACK_WIN_ACTIVE_MS, 0));
}

static void test_reply_does_not_extend_background_ack_window(void) {
    const uint32_t now = 500000;
    assert(espnow_link_fresh(now, now - 10000, ESPNOW_ACK_WIN_BG_MS, 0));
    assert(!espnow_link_fresh(now, 0, ESPNOW_ACK_WIN_BG_MS, now - 10000));
}

static void test_reply_freshness_wraparound(void) {
    const uint32_t last_rx = UINT32_MAX - 1000;
    assert(espnow_link_fresh(500, 0, ESPNOW_ACK_WIN_ACTIVE_MS, last_rx));
    assert(!espnow_link_fresh(last_rx + ESPNOW_ACK_WIN_ACTIVE_MS,
                             0, ESPNOW_ACK_WIN_ACTIVE_MS, last_rx));
}

static void test_only_complete_compatible_state_is_contact(void) {
    uint8_t frame[sizeof(struct sl2_state_pkt)] = {SL2_PKT_STATE, SL2_PROTO_VERSION};
    assert(espnow_state_reply(frame, sizeof frame));
    assert(espnow_state_reply(frame, SL2_STATE_MIN_LEN));
    assert(!espnow_state_reply(frame, SL2_STATE_MIN_LEN - 1));
    assert(!espnow_state_reply(NULL, sizeof frame));
    frame[1] = SL2_PROTO_MIN_COMPAT - 1;
    assert(!espnow_state_reply(frame, sizeof frame));

    /* This header promises an absent catalog entry and is rejected later.
     * It must not refresh the timestamp used to keep the connection online. */
    struct sl2_room_catalog_resp_hdr catalog = {
        .type = SL2_PKT_ROOM_CATALOG_RESP, .version = SL2_PROTO_VERSION, .count = 1
    };
    assert(!espnow_state_reply((const uint8_t *)&catalog, sizeof catalog));
    frame[0] = SL2_PKT_INFO;
    frame[1] = SL2_PROTO_VERSION;
    assert(!espnow_state_reply(frame, sizeof frame));
}

/* Observed: after the controller's AP roamed, the dial sat on ch 5
 * with the controller on ch 6. `zones` showed ack <1 s, synced=0 and no STATE
 * for 5 h: the neighbouring channel returns radio ACKs but no application
 * traffic, and nothing made the dial look elsewhere. */
static void test_acked_but_silent_zone_still_hunts(void) {
    assert(espnow_hunt_due(true, true, false));
    assert(espnow_hunt_due(true, false, true));
}

static void test_lost_zone_hunts(void) {
    assert(espnow_hunt_due(false, false, false));
}

static void test_healthy_zone_does_not_hunt(void) {
    assert(!espnow_hunt_due(true, false, false));
}

static void test_ack_alone_does_not_lock_a_channel(void) {
    assert(!espnow_lock_ok(true, false));
    assert(espnow_lock_ok(true, true));
    assert(!espnow_lock_ok(false, false));
}

static void test_reply_must_be_heard_on_the_probed_channel(void) {
    const uint32_t asked = 500000;
    assert(espnow_reply_since(asked, asked + 40, 6, 6));
    assert(espnow_reply_since(asked, asked, 6, 6));
    assert(!espnow_reply_since(asked, asked + 40, 6, 5));
    assert(!espnow_reply_since(asked, asked + 40, 0, 0));
}

static void test_reply_older_than_the_question_is_not_an_answer(void) {
    const uint32_t asked = 500000;
    assert(!espnow_reply_since(asked, asked - 1, 6, 6));
    assert(!espnow_reply_since(asked, asked - 20000, 6, 6));
    assert(!espnow_reply_since(asked, 0, 6, 6));
}

static void test_reply_since_u32_wraparound(void) {
    const uint32_t asked = UINT32_MAX - 100;
    assert(espnow_reply_since(asked, 50, 6, 6));
    assert(!espnow_reply_since(50, asked, 6, 6));
}

static void test_confirm_wait_covers_the_controller_state_floor(void) {
    /* serin-link-core serves a STATE pull no sooner than 250 ms after its last
     * STATE (SL2_STATE_MIN_INTERVAL_MS), from a loop that runs at about 20 Hz. */
    assert(ESPNOW_CH_CONFIRM_MS >= 250 + 50);
    assert(ESPNOW_CH_CONFIRM_MS <= 1000);
}

static void test_unconfirmed_ack_channel_is_not_remembered(void) {
    /* "Channel heal" stored the neighbour's channel, so even a
     * reboot started from it. */
    assert(!espnow_heal_due(true, 5, 6, false));
    assert(espnow_heal_due(true, 6, 5, true));
}

static void test_heal_needs_a_fresh_ack_and_a_change(void) {
    assert(!espnow_heal_due(true, 6, 6, true));
    assert(!espnow_heal_due(false, 6, 5, true));
    assert(!espnow_heal_due(true, 0, 5, true));
}

int main(void) {
    test_only_complete_compatible_state_is_contact();
    test_fresh_controller_reply_survives_missed_radio_acks();
    test_reply_evidence_expires_without_hiding_silence();
    test_reply_does_not_extend_background_ack_window();
    test_reply_freshness_wraparound();
    test_not_synced_never_stale();
    test_fresh_rx_not_stale();
    test_silent_app_is_stale();
    test_unpair_scenario();
    test_u32_wraparound();
    test_threshold_covers_missed_heartbeats();
    test_unsynced_zone_wants_state();
    test_fresh_zone_does_not_pull();
    test_stale_zone_pulls_to_refresh();
    test_never_acked_is_not_fresh();
    test_window_matches_cadence();
    test_fresh_ack_within_its_window();
    test_demoted_zone_cannot_flip_online();
    test_promoted_zone_stays_honest_for_the_bg_window();
    test_promoted_zone_evidence_must_not_gate_the_sweep();
    test_ack_u32_wraparound();
    test_lone_ack_is_not_corroborated();
    test_second_ack_same_channel_corroborates();
    test_second_ack_other_channel_does_not();
    test_stale_prior_ack_does_not_corroborate();
    test_corrob_window_covers_the_bg_cadence();
    test_corrob_u32_wraparound();
    test_never_probed_is_checking();
    test_recently_probed_never_heard_is_checking();
    test_grace_expired_is_not_checking();
    test_any_ack_ends_checking();
    test_synced_zone_is_not_checking();
    test_checking_u32_wraparound();
    test_check_grace_is_a_sane_window();
    test_switch_warm_needs_all_three();
    test_switch_cold_when_unsynced();
    test_switch_cold_when_ack_stale();
    test_switch_cold_when_rx_stale();
    test_acked_but_silent_zone_still_hunts();
    test_lost_zone_hunts();
    test_healthy_zone_does_not_hunt();
    test_ack_alone_does_not_lock_a_channel();
    test_reply_must_be_heard_on_the_probed_channel();
    test_reply_older_than_the_question_is_not_an_answer();
    test_reply_since_u32_wraparound();
    test_confirm_wait_covers_the_controller_state_floor();
    test_unconfirmed_ack_channel_is_not_remembered();
    test_heal_needs_a_fresh_ack_and_a_change();
    printf("espnow_liveness: all tests passed\n");
    return 0;
}
