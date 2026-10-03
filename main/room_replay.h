/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "sl2_proto.h"

/* Old controllers tolerate additive tails and may not publish an epoch.
 * v5 controllers require a nonzero echo even before the first command. */
static inline bool room_epoch_ready(uint8_t peer_version, uint16_t epoch) {
    return peer_version >= SL2_PROTO_MIN_COMPAT &&
           (peer_version < SL2_ROOM_EPOCH_MIN_VER || epoch != 0);
}

/* Selection packets capture the epoch at the user's action. Never refresh
 * that echo during queue drain: a reboot invalidates the old action. */
static inline bool room_source_tx_current(const uint8_t *data, size_t len,
                                           uint8_t peer_version, uint16_t epoch) {
    if (len < sizeof(struct sl2_room_source_set_pkt) ||
        !room_epoch_ready(peer_version, epoch)) return false;
    struct sl2_room_source_set_pkt p;
    sl2_decode_pkt(&p, sizeof p, data, (int)len);
    return p.type == SL2_PKT_ROOM_SOURCE_SET && p.epoch == epoch;
}
