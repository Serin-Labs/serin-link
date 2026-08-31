/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sl2_sha256.h"
#include "sl2_proto.h"

static void hex2bin(const char *hex, uint8_t *out) {
    for (size_t i = 0; hex[2 * i]; i++) {
        unsigned b;
        sscanf(hex + 2 * i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

static void check_hkdf(const uint8_t *ikm, size_t il, const uint8_t *salt, size_t sl,
                       const uint8_t *info, size_t fl, size_t L, const char *want_hex) {
    uint8_t okm[64], want[64];
    assert(L <= sizeof okm && strlen(want_hex) == 2 * L);
    sl2_hkdf_sha256(ikm, il, salt, sl, info, fl, okm, L);
    hex2bin(want_hex, want);
    assert(memcmp(okm, want, L) == 0);
}

static void test_rfc5869_tc1(void) {
    uint8_t ikm[22]; memset(ikm, 0x0b, sizeof ikm);
    uint8_t salt[13]; for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    uint8_t info[10]; for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
    check_hkdf(ikm, sizeof ikm, salt, sizeof salt, info, sizeof info, 42,
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
        "34007208d5b887185865");
}

static void test_rfc5869_tc3_empty_salt_info(void) {
    uint8_t ikm[22]; memset(ikm, 0x0b, sizeof ikm);
    check_hkdf(ikm, sizeof ikm, NULL, 0, NULL, 0, 42,
        "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
        "9d201395faa4b61a96c8");
}

static void test_lmk_shape(void) {
    uint8_t ikm[32];  memset(ikm, 0x01, sizeof ikm);
    uint8_t dial[32]; memset(dial, 0x02, sizeof dial);
    uint8_t ctrl[32]; memset(ctrl, 0x03, sizeof ctrl);
    uint8_t salt[64];
    sl2_lmk_salt(dial, ctrl, salt);
    assert(salt[0] == 0x02 && salt[31] == 0x02 &&
           salt[32] == 0x03 && salt[63] == 0x03);
    assert(SL2_LMK_INFO_LEN == strlen(SL2_LMK_INFO));
    check_hkdf(ikm, sizeof ikm, salt, sizeof salt,
               (const uint8_t *)SL2_LMK_INFO, SL2_LMK_INFO_LEN, 16,
               "669d5e7556e3401cc9ace9af7c8e9647");
}

int main(void) {
    test_rfc5869_tc1();
    test_rfc5869_tc3_empty_salt_info();
    test_lmk_shape();
    printf("sl2_sha256: all tests passed\n");
    return 0;
}
