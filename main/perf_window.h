/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PERF_RENDER, PERF_FLUSH_CALL, PERF_FLUSH_WAIT, PERF_TRANSFER, PERF_METRIC_COUNT
} perf_metric_t;

typedef struct {
    uint32_t count, max_us;
    uint64_t total_us;
} perf_sample_t;

/* The caller serializes event, ISR-completion, and snapshot access. */
typedef struct {
    perf_sample_t samples[PERF_METRIC_COUNT];
    int64_t started[PERF_METRIC_COUNT];
    bool active[PERF_METRIC_COUNT];
    uint32_t transfer_bytes;
    uint64_t completed_bytes;
} perf_window_t;

static inline void perf_window_start(perf_window_t *w, perf_metric_t m, int64_t us)
{
    w->started[m] = us;
    w->active[m] = true;
}
static inline void perf_window_start_transfer(perf_window_t *w, int64_t us, uint32_t bytes)
{
    perf_window_start(w, PERF_TRANSFER, us);
    w->transfer_bytes = bytes;
}
static inline void perf_window_end(perf_window_t *w, perf_metric_t m, int64_t us)
{
    if (!w->active[m]) return;
    w->active[m] = false;
    uint32_t elapsed = (uint32_t)(us - w->started[m]);
    perf_sample_t *s = &w->samples[m];
    s->count++;
    s->total_us += elapsed;
    if (elapsed > s->max_us) s->max_us = elapsed;
    if (m == PERF_TRANSFER) w->completed_bytes += w->transfer_bytes;
}
static inline void perf_window_take(perf_window_t *w, perf_sample_t *out, uint64_t *bytes)
{
    for (int i = 0; i < PERF_METRIC_COUNT; i++) {
        out[i] = w->samples[i];
        w->samples[i] = (perf_sample_t){0};
    }
    *bytes = w->completed_bytes;
    w->completed_bytes = 0;
    /* Keep active samples: a console snapshot can interrupt a DMA transfer or
     * an LVGL wait. Assign that sample to the window in which it completes. */
}
static inline unsigned perf_cpu_tenths(uint32_t now, uint32_t before, uint32_t elapsed)
{
    return elapsed ? (unsigned)((uint64_t)(uint32_t)(now - before) * 1000 / elapsed) : 0;
}
