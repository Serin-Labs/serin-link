/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "perf_window.h"

int main(void)
{
    perf_window_t w = {0};
    perf_sample_t samples[PERF_METRIC_COUNT];
    uint64_t bytes;
    /* An ISR completion before instrumentation starts must not become a sample. */
    perf_window_end(&w, PERF_TRANSFER, 100);
    perf_window_take(&w, samples, &bytes);
    assert(samples[PERF_TRANSFER].count == 0 && bytes == 0);

    perf_window_start(&w, PERF_FLUSH_CALL, 1000);
    perf_window_start_transfer(&w, 1000, 55920);
    perf_window_end(&w, PERF_FLUSH_CALL, 1010);
    /* Returning from the callback is not DMA completion. */
    perf_window_take(&w, samples, &bytes);
    assert(samples[PERF_FLUSH_CALL].count == 1);
    assert(samples[PERF_FLUSH_CALL].total_us == 10);
    assert(samples[PERF_TRANSFER].count == 0 && bytes == 0);
    /* Resetting the counters cannot lose a transfer already in flight. */
    perf_window_end(&w, PERF_TRANSFER, 2400);
    perf_window_end(&w, PERF_TRANSFER, 2500); /* Ignore a duplicate completion. */
    perf_window_take(&w, samples, &bytes);
    assert(samples[PERF_TRANSFER].count == 1);
    assert(samples[PERF_TRANSFER].total_us == 1400);
    assert(samples[PERF_TRANSFER].max_us == 1400 && bytes == 55920);
    assert(samples[PERF_FLUSH_CALL].count == 0);

    perf_window_start(&w, PERF_FLUSH_WAIT, 5000);
    perf_window_take(&w, samples, &bytes);
    perf_window_end(&w, PERF_FLUSH_WAIT, 5200);
    perf_window_start(&w, PERF_FLUSH_WAIT, 5500);
    perf_window_end(&w, PERF_FLUSH_WAIT, 5600);
    perf_window_take(&w, samples, &bytes);
    assert(samples[PERF_FLUSH_WAIT].count == 2);
    assert(samples[PERF_FLUSH_WAIT].total_us == 300);
    assert(samples[PERF_FLUSH_WAIT].max_us == 200);
    assert(bytes == 0);

    /* Long runs and completed-byte totals must not overflow at 32 bits. */
    for (int i = 0; i < 2; i++) {
        perf_window_start_transfer(&w, 0, UINT32_MAX);
        perf_window_end(&w, PERF_TRANSFER, UINT32_MAX);
    }
    perf_window_take(&w, samples, &bytes);
    assert(samples[PERF_TRANSFER].total_us == UINT64_C(8589934590));
    assert(bytes == UINT64_C(8589934590));
    /* FreeRTOS runtime counters can wrap during a measurement interval. */
    assert(perf_cpu_tenths(100, UINT32_MAX - 99, 1000) == 200);
    assert(perf_cpu_tenths(1500, 1000, 2000) == 250);
    assert(perf_cpu_tenths(1500, 1000, 0) == 0);
    puts("perf_window: OK");
}
