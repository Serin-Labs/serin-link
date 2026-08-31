/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <string.h>
#include <stdbool.h>

#define RIMLIST_MAX 8

typedef struct { int ids[RIMLIST_MAX]; int n; } rimlist_t;
typedef bool (*rimlist_keep_fn)(int id);

static inline void rimlist_build(rimlist_t *rl, int cap, rimlist_keep_fn keep) {
    rl->n = 0;
    for (int id = 0; id < cap && rl->n < RIMLIST_MAX; id++)
        if (keep(id)) rl->ids[rl->n++] = id;
}

static inline int rimlist_id(const rimlist_t *rl, int pos) { return rl->ids[pos]; }

static inline int rimlist_pos(const rimlist_t *rl, int id) {
    for (int i = 0; i < rl->n; i++) if (rl->ids[i] == id) return i;
    return 0;
}

static inline int rimlist_wrap(int n, int pos) { return ((pos % n) + n) % n; }

static inline bool rimlist_moved(rimlist_t *rl, int cap, rimlist_keep_fn keep) {
    int old_n = rl->n; int old_ids[RIMLIST_MAX];
    memcpy(old_ids, rl->ids, sizeof(int) * (size_t)old_n);
    rimlist_build(rl, cap, keep);
    return !(old_n == rl->n &&
             memcmp(old_ids, rl->ids, sizeof(int) * (size_t)rl->n) == 0);
}
