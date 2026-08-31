/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "espnow_bond.h"
#include <stdbool.h>
#include <string.h>
#include <nvs_flash.h>
#include "esp_log.h"

static const char *TAG = "espnow_bond";
static const char *NS  = "espnow";

static espnow_bond_rec_t s_tab[ESPNOW_MAX_BONDS];
static int s_n = -1;

static bool persist(void) {
    uint8_t blob[ESPNOW_BONDS_BLOB_MAX];
    size_t len = espnow_bonds_encode(s_tab, s_n, blob, sizeof blob);
    nvs_handle_t h;
    if (!len || nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "bond persist failed"); return false;
    }
    bool ok = nvs_set_blob(h, "bonds", blob, len) == ESP_OK;
    if (ok) ok = nvs_commit(h) == ESP_OK;
    else    nvs_commit(h);
    if (!ok)
        ESP_LOGE(TAG, "bonds write failed");
    nvs_close(h);
    return ok;
}

static void load_once(void) {
    if (s_n >= 0) return;
    s_n = 0;
    memset(s_tab, 0, sizeof s_tab);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t blob[ESPNOW_BONDS_BLOB_MAX]; size_t bl = sizeof blob;
    if (nvs_get_blob(h, "bonds", blob, &bl) == ESP_OK) {
        int n = espnow_bonds_decode(blob, bl, s_tab);
        if (n >= 0) s_n = n;
        else ESP_LOGW(TAG, "bonds blob not fmt %d (pre-v2?) - starting empty",
                      ESPNOW_BOND_FMT);
    }
    nvs_close(h);
}

int espnow_bonds_load(espnow_bond_rec_t out[ESPNOW_MAX_BONDS]) {
    load_once();
    memcpy(out, s_tab, sizeof s_tab);
    return s_n;
}

int espnow_bond_add(const espnow_bond_rec_t *r) {
    load_once();
    int i = espnow_bonds_find(s_tab, s_n, r->mac);
    bool grew = false;
    if (i < 0) {
        if (s_n >= ESPNOW_MAX_BONDS) return -1;
        i = s_n++; grew = true;
    }
    espnow_bond_rec_t prev = s_tab[i];
    s_tab[i] = *r;
    if (!persist()) {
        if (grew) { s_n--; memset(&s_tab[i], 0, sizeof s_tab[0]); }
        else s_tab[i] = prev;
        return -2;
    }
    ESP_LOGI(TAG, "bond %d saved %02X:%02X:%02X:%02X:%02X:%02X", i,
             r->mac[0], r->mac[1], r->mac[2], r->mac[3], r->mac[4], r->mac[5]);
    return i;
}

void espnow_bond_remove(int idx) {
    load_once();
    if (idx < 0 || idx >= s_n) return;
    memmove(&s_tab[idx], &s_tab[idx + 1],
            (size_t)(s_n - idx - 1) * sizeof s_tab[0]);
    s_n--;
    memset(&s_tab[s_n], 0, sizeof s_tab[0]);
    persist();
}

void espnow_bond_set_channel(int idx, uint8_t ch) {
    load_once();
    if (idx < 0 || idx >= s_n || s_tab[idx].last_channel == ch) return;
    s_tab[idx].last_channel = ch;
    persist();
}

void espnow_bond_set_name(int idx, const char *name, uint8_t caps_seq) {
    load_once();
    if (idx < 0 || idx >= s_n) return;
    if (s_tab[idx].caps_seq == caps_seq &&
        strncmp(s_tab[idx].name, name, sizeof s_tab[idx].name) == 0) return;
    strncpy(s_tab[idx].name, name, sizeof s_tab[idx].name - 1);
    s_tab[idx].name[sizeof s_tab[idx].name - 1] = '\0';
    s_tab[idx].caps_seq = caps_seq;
    persist();
}
