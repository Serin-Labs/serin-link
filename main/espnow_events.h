/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <esp_now.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

enum { EV_RX, EV_CMD, EV_TX, EV_WAKE };
typedef struct {
    uint8_t kind;
    uint8_t zi;
    uint8_t len;
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
} evt_t;

/* Keep wake coalescing with the queue whose contents it describes. */
typedef struct {
    QueueHandle_t handle;
    volatile bool wake_queued;
} espnow_events_t;

static inline bool espnow_events_init(espnow_events_t *q) {
    if (!q->handle) q->handle = xQueueCreate(8, sizeof(evt_t));
    return q->handle != NULL;
}

static inline bool espnow_events_post(espnow_events_t *q, const evt_t *it) {
    if (!q->handle) return false;
    if (it->kind == EV_WAKE) {
        if (q->wake_queued) return true;
        q->wake_queued = true;
    }
    if (xQueueSend(q->handle, it, 0) == pdTRUE) return true;
    if (it->kind == EV_WAKE) q->wake_queued = false;
    return false;
}

static inline bool espnow_events_receive(espnow_events_t *q, evt_t *it,
                                         TickType_t wait) {
    if (!q->handle || xQueueReceive(q->handle, it, wait) != pdTRUE) return false;
    if (it->kind == EV_WAKE) q->wake_queued = false;
    return true;
}

static inline void espnow_events_reset(espnow_events_t *q) {
    if (q->handle) xQueueReset(q->handle);
    /* A discarded EV_WAKE must not suppress every later interval. Clear after
     * the queue: a callback arriving during reset can otherwise leave the
     * latch set for an event that reset just removed. At worst, a concurrent
     * post leaves an extra wake queued; it cannot stop future wake delivery. */
    q->wake_queued = false;
}
