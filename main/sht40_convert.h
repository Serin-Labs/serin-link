/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint8_t sht40_crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

static inline int16_t sht40_raw_to_dc(uint16_t raw)
{
    const long num = -450L * 65535L + 1750L * (long)raw;
    return (int16_t)((num >= 0) ? (num + 32767) / 65535 : (num - 32767) / 65535);
}

static inline int16_t sht40_raw_to_cc(uint16_t raw)
{
    const long num = -4500L * 65535L + 17500L * (long)raw;
    return (int16_t)((num >= 0) ? (num + 32767) / 65535 : (num - 32767) / 65535);
}

static inline uint8_t sht40_raw_to_rh_pct(uint16_t raw)
{
    const long num = -600L * 65535L + 12500L * (long)raw;
    long pct = ((num >= 0) ? (num + 3276750) / 6553500 : (num - 3276750) / 6553500);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

static inline uint16_t sht40_raw_to_rh_ccpct(uint16_t raw)
{
    const long num = -600L * 65535L + 12500L * (long)raw;
    long cc = ((num >= 0) ? (num + 32767) / 65535 : (num - 32767) / 65535);
    if (cc < 0)     cc = 0;
    if (cc > 10000) cc = 10000;
    return (uint16_t)cc;
}

static inline uint16_t sht40_rh_apply_gain(uint16_t rh_cc, uint16_t gain_ppt)
{
    const uint32_t v = ((uint32_t)rh_cc * (uint32_t)gain_ppt + 500u) / 1000u;
    return (v > 10000u) ? (uint16_t)10000 : (uint16_t)v;
}

static inline bool sht40_parse(const uint8_t rx[6], int16_t *temp_cc, uint16_t *rh_ccpct)
{
    if (sht40_crc8(rx, 2) != rx[2])     return false;
    if (sht40_crc8(rx + 3, 2) != rx[5]) return false;
    *temp_cc  = sht40_raw_to_cc((uint16_t)((rx[0] << 8) | rx[1]));
    *rh_ccpct = sht40_raw_to_rh_ccpct((uint16_t)((rx[3] << 8) | rx[4]));
    return true;
}

static inline int16_t sht40_offset_dc_to_cc(int16_t dc) { return (int16_t)(dc * 10); }

static inline int16_t sht40_offset_eff(int16_t base, bool idle_off, int16_t delta_cc)
{
    return (int16_t)(idle_off ? base - delta_cc : base);
}

static inline int32_t sht40_q8_from_cc(int16_t cc) { return (int32_t)cc * 256; }

static inline int32_t sht40_lpf_step_q8(int32_t cur_q8, int16_t target_cc,
                                        uint32_t dt_ms, uint32_t tau_ms)
{
    const int32_t tgt_q8 = sht40_q8_from_cc(target_cc);
    if (dt_ms >= tau_ms) return tgt_q8;
    return cur_q8 + (int32_t)(((int64_t)(tgt_q8 - cur_q8) * dt_ms) / tau_ms);
}

static inline int16_t sht40_q8_to_cc(int32_t q8)
{
    return (int16_t)((q8 >= 0 ? q8 + 128 : q8 - 128) / 256);
}
