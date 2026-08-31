/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "link_crypto.h"
#include <string.h>
#include <sodium.h>
#include "sl2_sha256.h"
#include "sl2_proto.h"
#include <nvs.h>
#include "esp_log.h"
#include "esp_efuse.h"
#include "esp_hmac.h"

static const char *TAG = "link_crypto";

static bool ensure_sodium(void) {
    static int rc = -2;
    if (rc == -2) rc = sodium_init();
    return rc >= 0;
}

static int efuse_hmac_key(void) {
    static int cached = -2;
    if (cached != -2) return cached;
    esp_efuse_block_t blk;
    cached = esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &blk)
                 ? (int)(HMAC_KEY0 + (blk - EFUSE_BLK_KEY0)) : -1;
    return cached;
}

link_id_source_t link_crypto_identity_source(void) {
    return efuse_hmac_key() >= 0 ? LINK_ID_EFUSE : LINK_ID_NVS;
}

bool link_crypto_identity(uint8_t priv[64], uint8_t pub[32]) {
    if (!ensure_sodium()) return false;
    int key = efuse_hmac_key();
    if (key >= 0) {
        static const uint8_t label[16] = "serin-link-id-v1";
        uint8_t seed[32];
        if (esp_hmac_calculate((hmac_key_id_t)key, label, sizeof label,
                               seed) == ESP_OK &&
            crypto_sign_seed_keypair(pub, priv, seed) == 0) {
            sodium_memzero(seed, sizeof seed);
            return true;
        }
        sodium_memzero(seed, sizeof seed);
        ESP_LOGE(TAG, "eFuse identity derivation failed; falling back to NVS");
    }
    nvs_handle_t h;
    uint8_t id[96];
    size_t len = sizeof id;
    if (nvs_open("espnow", NVS_READWRITE, &h) != ESP_OK) return false;
    if (nvs_get_blob(h, "idkey", id, &len) == ESP_OK && len == sizeof id) {
        memcpy(priv, id, 64);
        memcpy(pub, id + 64, 32);
        nvs_close(h);
        sodium_memzero(id, sizeof id);
        return true;
    }
    if (crypto_sign_ed25519_keypair(pub, priv) != 0) { nvs_close(h); return false; }
    memcpy(id, priv, 64);
    memcpy(id + 64, pub, 32);
    bool ok = nvs_set_blob(h, "idkey", id, sizeof id) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    sodium_memzero(id, sizeof id);
    if (ok) ESP_LOGI(TAG, "generated dial identity");
    return ok;
}

int link_crypto_x25519_keypair(uint8_t priv[32], uint8_t pub[32]) {
    if (!ensure_sodium()) return -1;
    randombytes_buf(priv, 32);
    return crypto_scalarmult_curve25519_base(pub, priv);
}

int link_crypto_derive_lmk(const uint8_t own_eph_priv[32],
                           const uint8_t peer_eph_pub[32],
                           const uint8_t dial_eph_pub[32],
                           const uint8_t ctrl_eph_pub[32],
                           uint8_t lmk[16]) {
    if (!ensure_sodium()) return -1;
    uint8_t shared[32], salt[64];
    if (crypto_scalarmult_curve25519(shared, own_eph_priv, peer_eph_pub) != 0)
        return -1;
    sl2_lmk_salt(dial_eph_pub, ctrl_eph_pub, salt);
    sl2_hkdf_sha256(shared, 32, salt, 64,
                    (const uint8_t *)SL2_LMK_INFO, SL2_LMK_INFO_LEN, lmk, 16);
    sodium_memzero(shared, sizeof shared);
    return 0;
}

int link_crypto_sign(const uint8_t priv[64],
                     const uint8_t *msg, size_t len, uint8_t sig[64]) {
    if (!ensure_sodium()) return -1;
    return crypto_sign_ed25519_detached(sig, NULL, msg, len, priv);
}

int link_crypto_verify(const uint8_t pub[32],
                       const uint8_t *msg, size_t len, const uint8_t sig[64]) {
    if (!ensure_sodium()) return -1;
    return crypto_sign_ed25519_verify_detached(sig, msg, len, pub);
}
