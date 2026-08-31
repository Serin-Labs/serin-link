/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "espnow_bond.h"

static espnow_bond_rec_t mk(uint8_t tag) {
    espnow_bond_rec_t r; memset(&r, 0, sizeof r);
    memset(r.mac, tag, 6); memset(r.lmk, tag ^ 0xFF, 16); memset(r.peer_id_pub, tag ^ 0x55, 32); r.caps_seq = tag;
    r.last_channel = tag % 14;
    snprintf(r.name, sizeof r.name, "Zone %u", (unsigned)tag);
    return r;
}

static void test_sizes(void) {
    assert(sizeof(espnow_bond_rec_t) == 104);
    assert(ESPNOW_MAX_BONDS == 7);
    assert(ESPNOW_BONDS_BLOB_MAX == 2 + 7 * 104);
}

static void test_roundtrip(void) {
    espnow_bond_rec_t in[ESPNOW_MAX_BONDS], out[ESPNOW_MAX_BONDS];
    for (int i = 0; i < ESPNOW_MAX_BONDS; i++) in[i] = mk((uint8_t)(i + 1));
    uint8_t blob[ESPNOW_BONDS_BLOB_MAX];
    for (int n = 0; n <= ESPNOW_MAX_BONDS; n++) {
        size_t len = espnow_bonds_encode(in, n, blob, sizeof blob);
        assert(len == 2 + (size_t)n * 104);
        assert(espnow_bonds_decode(blob, len, out) == n);
        assert(memcmp(out, in, (size_t)n * sizeof *in) == 0);
    }
}

static void test_reject(void) {
    espnow_bond_rec_t in[1] = { mk(1) }, out[ESPNOW_MAX_BONDS];
    uint8_t blob[ESPNOW_BONDS_BLOB_MAX];
    size_t len = espnow_bonds_encode(in, 1, blob, sizeof blob);
    assert(len == 106);
    blob[0] = 1; assert(espnow_bonds_decode(blob, len, out) == -1);
    blob[0] = ESPNOW_BOND_FMT;
    assert(espnow_bonds_decode(blob, len - 1, out) == -1);
    assert(espnow_bonds_decode(blob, 1, out) == -1);
    blob[1] = ESPNOW_MAX_BONDS + 1;
    assert(espnow_bonds_decode(blob, len, out) == -1);
    blob[1] = 1;
    assert(espnow_bonds_encode(in, ESPNOW_MAX_BONDS + 1, blob, sizeof blob) == 0);
    uint8_t small[10];
    assert(espnow_bonds_encode(in, 1, small, sizeof small) == 0);
}

static void test_find(void) {
    espnow_bond_rec_t t[3] = { mk(1), mk(2), mk(3) };
    uint8_t mac[6]; memset(mac, 2, 6);
    assert(espnow_bonds_find(t, 3, mac) == 1);
    memset(mac, 9, 6);
    assert(espnow_bonds_find(t, 3, mac) == -1);
    assert(espnow_bonds_find(t, 0, mac) == -1);
}

int main(void) {
    test_sizes(); test_roundtrip(); test_reject(); test_find();
    printf("test_espnow_bond: all tests passed\n");
    return 0;
}
