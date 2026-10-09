/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sl2_proto.h"

#define ESPNOW_RX_STALE_MS 25000

static inline bool espnow_rx_stale(uint32_t now_ms, uint32_t last_rx_ms,
                                   bool synced) {
    return synced && (uint32_t)(now_ms - last_rx_ms) >= ESPNOW_RX_STALE_MS;
}

#define ESPNOW_ACK_WIN_ACTIVE_MS  4000
#define ESPNOW_ACK_WIN_BG_MS     30000

static inline uint32_t espnow_ack_window(bool active_cadence) {
    return active_cadence ? ESPNOW_ACK_WIN_ACTIVE_MS : ESPNOW_ACK_WIN_BG_MS;
}

static inline bool espnow_ack_fresh(uint32_t now_ms, uint32_t last_ack_ms,
                                    uint32_t ack_win_ms) {
    return last_ack_ms != 0 && (uint32_t)(now_ms - last_ack_ms) < ack_win_ms;
}

static inline bool espnow_link_fresh(uint32_t now_ms, uint32_t last_ack_ms,
                                     uint32_t ack_win_ms, uint32_t last_state_rx_ms) {
    /* An actual peer STATE reply also proves contact. Keep its grace short:
     * old state must not prolong liveness through a real interruption. */
    return espnow_ack_fresh(now_ms, last_ack_ms, ack_win_ms) ||
           (last_state_rx_ms != 0 &&
            (uint32_t)(now_ms - last_state_rx_ms) < ESPNOW_ACK_WIN_ACTIVE_MS);
}

/* Used only after the caller accepts a frame from its bonded peer. Other
 * packet types can still require deferred validation and are not evidence. */
static inline bool espnow_state_reply(const uint8_t *data, int len) {
    return data && len >= SL2_STATE_MIN_LEN && data[0] == SL2_PKT_STATE &&
           data[1] >= SL2_PROTO_MIN_COMPAT;
}

static inline bool espnow_want_state(uint32_t now_ms, uint32_t last_rx_ms,
                                     bool synced) {
    return !synced || espnow_rx_stale(now_ms, last_rx_ms, synced);
}

#define ESPNOW_ACK_CORROB_MS 5000

static inline bool espnow_ack_corroborated(uint32_t now_ms, uint32_t prev_ack_ms,
                                           uint8_t prev_ack_ch, uint8_t ch) {
    return ch == prev_ack_ch &&
           espnow_ack_fresh(now_ms, prev_ack_ms, ESPNOW_ACK_CORROB_MS);
}

#define ESPNOW_CHECK_GRACE_MS 4000

static inline bool espnow_zone_checking(uint32_t now_ms, uint32_t first_probe_ms,
                                        uint32_t last_ack_ms, bool synced) {
    if (last_ack_ms != 0 || synced) return false;
    return first_probe_ms == 0 ||
           (uint32_t)(now_ms - first_probe_ms) < ESPNOW_CHECK_GRACE_MS;
}

static inline bool espnow_switch_warm(bool synced, bool ack_fresh, bool rx_stale) {
    return synced && ack_fresh && !rx_stale;
}

/* A radio ACK also comes back from a neighbouring channel, where no application
 * frame gets through. Only a STATE reply heard on a channel proves the peer is
 * tuned to it. The wait covers the controller's 250 ms STATE floor plus one
 * pass of its loop. */
#define ESPNOW_CH_CONFIRM_MS 400

static inline bool espnow_reply_since(uint32_t since_ms, uint32_t last_state_rx_ms,
                                      uint8_t state_ch, uint8_t ch) {
    return ch != 0 && state_ch == ch && last_state_rx_ms != 0 &&
           (uint32_t)(last_state_rx_ms - since_ms) < 0x80000000u;
}

static inline bool espnow_lock_ok(bool acked, bool reply_on_channel) {
    return acked && reply_on_channel;
}

/* Fresh ACKs with a silent application are not contact: keep hunting. */
static inline bool espnow_hunt_due(bool link_fresh, bool stuck_sync, bool rx_stale) {
    return !link_fresh || stuck_sync || rx_stale;
}

static inline bool espnow_heal_due(bool acked, uint8_t ack_ch, uint8_t remembered,
                                   bool reply_on_ack_ch) {
    return acked && ack_ch != 0 && remembered != ack_ch && reply_on_ack_ch;
}
