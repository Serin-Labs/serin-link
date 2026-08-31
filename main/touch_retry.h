/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TOUCH_RETRY_FAIL_LIMIT 5

#define TOUCH_RETRY_PROBE_US (2 * 1000 * 1000)

#define TOUCH_RETRY_MAX_RECOVERIES 3

typedef struct {
    uint32_t fails;
    int64_t  last_probe_us;
    uint8_t  recoveries;
    bool     offline;
} touch_retry_t;

typedef enum {
    TOUCH_ACT_NONE = 0,
    TOUCH_ACT_RECOVER,
} touch_act_t;

static inline bool touch_retry_should_read(const touch_retry_t *t, int64_t now_us) {
    if (!t->offline) return true;
    return (now_us - t->last_probe_us) >= TOUCH_RETRY_PROBE_US;
}

static inline void touch_retry_note_read(touch_retry_t *t, int64_t now_us) {
    t->last_probe_us = now_us;
}

static inline touch_act_t touch_retry_note_fail(touch_retry_t *t) {
    if (++t->fails < TOUCH_RETRY_FAIL_LIMIT) return TOUCH_ACT_NONE;
    t->fails   = 0;
    t->offline = true;
    if (t->recoveries < TOUCH_RETRY_MAX_RECOVERIES) {
        t->recoveries++;
        return TOUCH_ACT_RECOVER;
    }
    return TOUCH_ACT_NONE;
}

static inline bool touch_retry_note_ok(touch_retry_t *t) {
    const bool recovered = t->offline;
    t->fails      = 0;
    t->recoveries = 0;
    t->offline    = false;
    return recovered;
}

static inline void touch_retry_retry_now(touch_retry_t *t) {
    t->last_probe_us -= TOUCH_RETRY_PROBE_US;
}
