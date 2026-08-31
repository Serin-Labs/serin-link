/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LINK_ID_NVS = 0, LINK_ID_EFUSE = 1 } link_id_source_t;
link_id_source_t link_crypto_identity_source(void);

bool link_crypto_identity(uint8_t priv[64], uint8_t pub[32]);

int  link_crypto_x25519_keypair(uint8_t priv[32], uint8_t pub[32]);

int  link_crypto_derive_lmk(const uint8_t own_eph_priv[32],
                            const uint8_t peer_eph_pub[32],
                            const uint8_t dial_eph_pub[32],
                            const uint8_t ctrl_eph_pub[32],
                            uint8_t lmk[16]);

int  link_crypto_sign(const uint8_t priv[64],
                      const uint8_t *msg, size_t len, uint8_t sig[64]);
int  link_crypto_verify(const uint8_t pub[32],
                        const uint8_t *msg, size_t len, const uint8_t sig[64]);

#ifdef __cplusplus
}
#endif
