/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
/* Host stand-in for the NVS API that espnow_bond.c uses. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK   0
#define ESP_FAIL (-1)
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *val, size_t len);
esp_err_t nvs_commit(nvs_handle_t h);
void      nvs_close(nvs_handle_t h);
