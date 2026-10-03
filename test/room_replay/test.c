/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include "room_replay.h"

int main(void) {
    assert(!room_epoch_ready(0, 0));
    assert(room_epoch_ready(3, 0));
    assert(room_epoch_ready(4, 0));
    assert(!room_epoch_ready(5, 0));
    assert(room_epoch_ready(5, 0x1234));
    struct sl2_room_source_set_pkt selected = {
        .type = SL2_PKT_ROOM_SOURCE_SET, .version = 5,
        .request_id = 1, .revision = 42, .source_id = 7, .epoch = 0x1234,
    };
    const uint8_t *wire = (const uint8_t *)&selected;
    assert(room_source_tx_current(wire, sizeof selected, 5, 0x1234));
    assert(!room_source_tx_current(wire, sizeof selected, 5, 0x5678));
    assert(!room_source_tx_current(wire, sizeof selected, 5, 0));
    assert(!room_source_tx_current(wire, 16, 5, 0x1234));
    assert(!room_source_tx_current(wire, 17, 5, 0x1234));
    /* Dropping queued work does not relabel it or mutate the user's choice. */
    assert(selected.epoch == 0x1234 && selected.source_id == 7);
    selected.epoch = 0x5678; /* a new user action after resync */
    assert(room_source_tx_current(wire, sizeof selected, 5, 0x5678));
    /* Older controller without epochs still receives the historical prefix. */
    selected.epoch = 0;
    assert(room_source_tx_current(wire, sizeof selected, 4, 0));
    assert(!room_source_tx_current(wire, sizeof selected, 5, 0));
    assert(!room_source_tx_current(wire, sizeof selected, 5, 0x5678));
    assert(offsetof(struct sl2_room_source_set_pkt, epoch) == 16);
    assert(offsetof(struct sl2_dial_sensor_pkt, epoch) == 9);
    assert(SL2_ROOM_SOURCE_SET_MIN_LEN == 16 && SL2_DIAL_SENSOR_MIN_LEN == 7);
    puts("room_replay: all tests passed");
    return 0;
}
