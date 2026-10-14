/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define ESPNOW_MAX_BONDS     7
#define ESPNOW_BOND_FMT      3
#define ESPNOW_BOND_NAME_LEN 32

typedef struct __attribute__((packed)) {
    uint8_t mac[6];
    uint8_t lmk[16];
    uint8_t peer_id_pub[32];
    uint8_t last_channel;
    char    name[ESPNOW_BOND_NAME_LEN];
    uint8_t caps_seq;
    uint8_t reserved[16];
} espnow_bond_rec_t;

#define ESPNOW_BONDS_BLOB_MAX (2 + ESPNOW_MAX_BONDS * (int)sizeof(espnow_bond_rec_t))

typedef char espnow_bond_sa_rec[(sizeof(espnow_bond_rec_t) == 104) ? 1 : -1];

static inline size_t espnow_bonds_encode(const espnow_bond_rec_t *r, int n,
                                         uint8_t *out, size_t cap) {
    if (n < 0 || n > ESPNOW_MAX_BONDS) return 0;
    size_t need = 2 + (size_t)n * sizeof(espnow_bond_rec_t);
    if (cap < need) return 0;
    out[0] = ESPNOW_BOND_FMT; out[1] = (uint8_t)n;
    memcpy(out + 2, r, need - 2);
    return need;
}

static inline int espnow_bonds_decode(const uint8_t *blob, size_t len,
                                      espnow_bond_rec_t out[ESPNOW_MAX_BONDS]) {
    if (len < 2 || blob[0] != ESPNOW_BOND_FMT) return -1;
    int n = blob[1];
    if (n > ESPNOW_MAX_BONDS ||
        len != 2 + (size_t)n * sizeof(espnow_bond_rec_t)) return -1;
    memset(out, 0, ESPNOW_MAX_BONDS * sizeof(espnow_bond_rec_t));
    memcpy(out, blob + 2, len - 2);
    return n;
}

static inline int espnow_bonds_find(const espnow_bond_rec_t *r, int n,
                                    const uint8_t mac[6]) {
    for (int i = 0; i < n; i++)
        if (memcmp(r[i].mac, mac, 6) == 0) return i;
    return -1;
}

int  espnow_bonds_load(espnow_bond_rec_t out[ESPNOW_MAX_BONDS]);
int  espnow_bond_add(const espnow_bond_rec_t *r);
void espnow_bond_remove(int idx);
/* Keyed by MAC: a remove shifts indices, so an index held across one could
 * land on another zone's record. Unknown MACs are ignored. */
void espnow_bond_set_channel(const uint8_t mac[6], uint8_t ch);
void espnow_bond_set_name(const uint8_t mac[6], const char *name, uint8_t caps_seq);
