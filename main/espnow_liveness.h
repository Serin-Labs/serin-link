/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

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
