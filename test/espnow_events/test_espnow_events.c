/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "espnow_events.h"

/* Only the FreeRTOS storage is simulated. Production owns wake coalescing,
 * delivery and reset; drive the same sequence as the held link task. */
struct fake_queue { evt_t items[8]; unsigned count; };
static struct fake_queue storage;
static espnow_events_t *wake_during_reset;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    assert(length == 8 && item_size == sizeof(evt_t));
    memset(&storage, 0, sizeof storage);
    return &storage;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t wait) {
    assert(wait == 0);
    if (q->count == 8) return pdFALSE;
    memcpy(&q->items[q->count++], item, sizeof(evt_t));
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t wait) {
    (void)wait;
    if (!q->count) return pdFALSE;
    memcpy(item, &q->items[0], sizeof(evt_t));
    --q->count;
    memmove(q->items, q->items + 1, q->count * sizeof(evt_t));
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t q) {
    if (wake_during_reset) {
        const evt_t wake = {.kind = EV_WAKE};
        espnow_events_post(wake_during_reset, &wake);
    }
    q->count = 0;
    return pdTRUE;
}

static espnow_events_t fresh(void) {
    espnow_events_t q = {0};
    wake_during_reset = NULL;
    assert(espnow_events_init(&q));
    return q;
}

static void expect_next_wake(espnow_events_t *q) {
    const evt_t wake = {.kind = EV_WAKE};
    evt_t got;
    assert(espnow_events_post(q, &wake));
    assert(espnow_events_receive(q, &got, 0));
    assert(got.kind == EV_WAKE);
}

static void test_resume_after_discarded_wake_delivers_next_interval(void) {
    espnow_events_t q = fresh();
    const evt_t stale_cmd = {.kind = EV_CMD, .len = 1, .data = {42}};
    const evt_t wake = {.kind = EV_WAKE};
    assert(espnow_events_post(&q, &stale_cmd));
    assert(espnow_events_post(&q, &wake));
    // The held task drops pre-resume events, including the pending wake.
    espnow_events_reset(&q);
    expect_next_wake(&q);
    expect_next_wake(&q);
    evt_t got;
    assert(!espnow_events_receive(&q, &got, 0));
}

static void test_wake_arriving_during_reset_cannot_latch_reception_off(void) {
    espnow_events_t q = fresh();
    // An event callback can run after resume starts but before the queue clears.
    wake_during_reset = &q;
    espnow_events_reset(&q);
    wake_during_reset = NULL;
    expect_next_wake(&q);
}

static void test_full_queue_does_not_suppress_future_wakes(void) {
    espnow_events_t q = fresh();
    for (unsigned i = 0; i < 8; ++i) {
        const evt_t rx = {.kind = EV_RX, .len = 1, .data = {i}};
        assert(espnow_events_post(&q, &rx));
    }
    const evt_t wake = {.kind = EV_WAKE};
    assert(!espnow_events_post(&q, &wake));
    for (unsigned i = 0; i < 8; ++i) {
        evt_t got;
        assert(espnow_events_receive(&q, &got, 0));
        assert(got.kind == EV_RX && got.len == 1 && got.data[0] == i);
    }
    expect_next_wake(&q);
}

static void test_pending_wakes_coalesce_until_consumed(void) {
    espnow_events_t q = fresh();
    const evt_t wake = {.kind = EV_WAKE};
    for (unsigned i = 0; i < 20; ++i) assert(espnow_events_post(&q, &wake));
    evt_t got;
    assert(espnow_events_receive(&q, &got, 0) && got.kind == EV_WAKE);
    assert(!espnow_events_receive(&q, &got, 0));
    expect_next_wake(&q);
}

int main(void) {
    test_resume_after_discarded_wake_delivers_next_interval();
    test_wake_arriving_during_reset_cannot_latch_reception_off();
    test_full_queue_does_not_suppress_future_wakes();
    test_pending_wakes_coalesce_until_consumed();
    puts("espnow_events: resume, reset race, full queue and coalescing passed");
    return 0;
}
