/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool prefs_get_bool(const char *key, bool def);
void prefs_set_bool(const char *key, bool val);

uint8_t prefs_get_u8(const char *key, uint8_t def);
void    prefs_set_u8(const char *key, uint8_t val);

int16_t prefs_get_i16(const char *key, int16_t def);
void    prefs_set_i16(const char *key, int16_t val);

/* Copies the stored string for `key` into `out` (always NUL-terminated).
   Falls back to `def` when the key is absent, empty, or does not fit `cap`.
   Returns the length written. */
size_t prefs_get_str(const char *key, char *out, size_t cap, const char *def);
void   prefs_set_str(const char *key, const char *val);

void prefs_erase(const char *key);
