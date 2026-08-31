/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool     on;
    int      val;
    uint32_t ms;
} edit_hold_t;

static inline void edit_hold_note(edit_hold_t *h, int val, uint32_t now_ms) {
    h->on = true; h->val = val; h->ms = now_ms;
}

static inline int edit_hold_echo(edit_hold_t *h, int echo, uint32_t now_ms,
                                 uint32_t win_ms) {
    if (!h->on) return echo;
    if (echo == h->val || (uint32_t)(now_ms - h->ms) > win_ms) {
        h->on = false;
        return echo;
    }
    return h->val;
}

static inline void edit_hold_drop(edit_hold_t *h) { h->on = false; }

typedef struct {
    bool     on;
    int      lo, hi;
    uint32_t ms;
} edit_hold2_t;

static inline void edit_hold2_note(edit_hold2_t *h, int lo, int hi,
                                   uint32_t now_ms) {
    h->on = true; h->lo = lo; h->hi = hi; h->ms = now_ms;
}

static inline void edit_hold2_echo(edit_hold2_t *h, int *lo, int *hi,
                                   uint32_t now_ms, uint32_t win_ms) {
    if (!h->on) return;
    if ((*lo == h->lo && *hi == h->hi) ||
        (uint32_t)(now_ms - h->ms) > win_ms) {
        h->on = false;
        return;
    }
    *lo = h->lo; *hi = h->hi;
}

static inline void edit_hold2_drop(edit_hold2_t *h) { h->on = false; }

static inline bool edit_hold_tx_due(const edit_hold_t *h, int sent_val,
                                    uint32_t now_ms, uint32_t debounce_ms,
                                    bool flush) {
    return h->on && h->val != sent_val &&
           (flush || (uint32_t)(now_ms - h->ms) >= debounce_ms);
}

static inline bool edit_hold2_tx_due(const edit_hold2_t *h, int sent_lo,
                                     int sent_hi, uint32_t now_ms,
                                     uint32_t debounce_ms, bool flush) {
    return h->on && (h->lo != sent_lo || h->hi != sent_hi) &&
           (flush || (uint32_t)(now_ms - h->ms) >= debounce_ms);
}

static inline bool edit_tx_allowed(int zone_count, bool synced) {
    return zone_count > 0 && synced;
}
