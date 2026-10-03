/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Owned by the radio worker, one per controller. UI entry IDs never go on air:
 * random wire tokens avoid colliding with a controller's tombstones on reboot. */
typedef struct {
    uint32_t ui, wire;
    bool cancelled;
} wifi_setup_session_t;

static inline bool wifi_setup_session_tx(wifi_setup_session_t *s, bool cancel,
                                         uint32_t ui, uint32_t random,
                                         uint32_t *wire)
{
    if (!ui) return false;
    if (ui != s->ui) {
        if (s->ui && (int32_t)(ui - s->ui) <= 0) return false;
        uint32_t token = random ? random : 1;
        if (token == s->wire && ++token == 0) token = 1;
        *s = (wifi_setup_session_t){ .ui = ui, .wire = token };
    }
    if (!cancel && s->cancelled) return false;
    if (cancel) s->cancelled = true;
    *wire = s->wire;
    return true;
}

static inline bool wifi_setup_session_ack(const wifi_setup_session_t *s,
                                          uint32_t wire, uint32_t *ui)
{
    if (!s->cancelled || !wire || wire != s->wire) return false;
    *ui = s->ui;
    return true;
}
