/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ESPNOW_CHSCAN_MISS_N 3

#define ESPNOW_CHSCAN_ROUND_REST_MS 5000

static inline bool espnow_chscan_due(uint8_t miss, bool online) {
    return !online && miss >= ESPNOW_CHSCAN_MISS_N;
}

#define ESPNOW_RELOCK_TRIES 3

#define ESPNOW_RELOCK_REST_MS 500

#define ESPNOW_RELOCK_FAST_ROUNDS 3

#define ESPNOW_RELOCK_REST_MAX_MS 30000

static inline uint32_t espnow_relock_rest_ms(uint8_t failed_rounds) {
    if (failed_rounds <= ESPNOW_RELOCK_FAST_ROUNDS) return ESPNOW_RELOCK_REST_MS;
    const unsigned shift = (unsigned)failed_rounds - ESPNOW_RELOCK_FAST_ROUNDS;
    if (shift >= 16) return ESPNOW_RELOCK_REST_MAX_MS;
    const uint32_t ms = (uint32_t)ESPNOW_RELOCK_REST_MS << shift;
    return ms > ESPNOW_RELOCK_REST_MAX_MS ? ESPNOW_RELOCK_REST_MAX_MS : ms;
}

static inline uint8_t espnow_relock_channel(int idx, uint8_t remembered) {
    if (idx < 0) return 0;
    const bool have_rem = remembered >= 1 && remembered <= 13;
    if (have_rem) {
        if (idx < ESPNOW_RELOCK_TRIES) return remembered;
        idx -= ESPNOW_RELOCK_TRIES;
    }
    for (uint8_t ch = 1; ch <= 13; ch++) {
        if (have_rem && ch == remembered) continue;
        if (idx-- == 0) return ch;
    }
    return 0;
}

static inline uint8_t espnow_chscan_channel(int idx, uint8_t home, uint8_t own,
                                            const uint8_t *sib, int nsib) {
    static const uint8_t pop[3] = { 1, 6, 11 };
    uint16_t seen = 0;
    if (home >= 1 && home <= 13) seen |= 1u << home;
    if (own  >= 1 && own  <= 13) seen |= 1u << own;
    int k = 0;
    for (int phase = 0; phase < 3; phase++) {
        int n = phase == 0 ? nsib : phase == 1 ? 3 : 13;
        for (int j = 0; j < n; j++) {
            uint8_t ch = phase == 0 ? sib[j]
                       : phase == 1 ? pop[j] : (uint8_t)(j + 1);
            if (ch < 1 || ch > 13 || (seen & (1u << ch))) continue;
            if (k++ == idx) return ch;
            seen |= 1u << ch;
        }
    }
    return 0;
}
