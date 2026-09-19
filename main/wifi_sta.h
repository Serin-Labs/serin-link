/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>

/* Join `ssid` as a station and wait up to 20 s for an IP. The Wi-Fi driver
 * must already be started, and only one caller may join at a time.
 *
 * A NULL, empty or overlong SSID (over 32 bytes), or a key over 64 bytes, fails
 * before the driver is touched: joining a truncated prefix would reach the
 * wrong network or send the wrong key. `cancel`, if not NULL, is polled every
 * 100 ms. On failure the station is left disconnected. */
bool wifi_sta_join(const char *ssid, const char *psk, const volatile bool *cancel);
