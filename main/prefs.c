/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "prefs.h"
#include <stdio.h>
#include <string.h>
#include "nvs.h"

static const char *NS = "serin";

bool prefs_get_bool(const char *key, bool def)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    uint8_t v = def ? 1 : 0;
    if (nvs_get_u8(h, key, &v) != ESP_OK) v = def ? 1 : 0;
    nvs_close(h);
    return v != 0;
}

void prefs_set_bool(const char *key, bool val)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, key, val ? 1 : 0) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

uint8_t prefs_get_u8(const char *key, uint8_t def)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    uint8_t v = def;
    if (nvs_get_u8(h, key, &v) != ESP_OK) v = def;
    nvs_close(h);
    return v;
}

void prefs_set_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_u8(h, key, val) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

int16_t prefs_get_i16(const char *key, int16_t def)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    int16_t v = def;
    if (nvs_get_i16(h, key, &v) != ESP_OK) v = def;
    nvs_close(h);
    return v;
}

void prefs_set_i16(const char *key, int16_t val)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i16(h, key, val) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

size_t prefs_get_str(const char *key, char *out, size_t cap, const char *def)
{
    nvs_handle_t h;
    if (cap == 0) return 0;
    out[0] = '\0';
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t len = cap;
        /* A key too long for `cap` returns ESP_ERR_NVS_INVALID_LENGTH having
           possibly written junk — treat any failure as absent. */
        if (nvs_get_str(h, key, out, &len) != ESP_OK) out[0] = '\0';
        nvs_close(h);
    }
    if (out[0] == '\0') snprintf(out, cap, "%s", def);
    return strlen(out);
}

void prefs_set_str(const char *key, const char *val)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, key, val) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

void prefs_erase(const char *key)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_erase_key(h, key) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}
