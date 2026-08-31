/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sl2_info_apply.h"

static size_t put_outside(uint8_t *buf, size_t cap, size_t off, int16_t dc) {
    uint8_t v[2] = { (uint8_t)(dc & 0xFF), (uint8_t)((dc >> 8) & 0xFF) };
    assert(sl2_tlv_put(buf, cap, &off, SL2_TLV_OUTSIDE_T, v, 2));
    return off;
}
static size_t put_runtime(uint8_t *buf, size_t cap, size_t off, uint32_t h) {
    uint8_t v[4];
    memcpy(v, &h, 4);
    assert(sl2_tlv_put(buf, cap, &off, SL2_TLV_RUNTIME, v, 4));
    return off;
}
static size_t put_batt(uint8_t *buf, size_t cap, size_t off, uint8_t pct) {
    assert(sl2_tlv_put(buf, cap, &off, SL2_TLV_SENSOR_BATT, &pct, 1));
    return off;
}
static size_t put_fw(uint8_t *buf, size_t cap, size_t off) {
    static const uint8_t v[] = "2026.6.1\0Jun 20 2026";
    assert(sl2_tlv_put(buf, cap, &off, SL2_TLV_FW_INFO, v, sizeof v - 1));
    return off;
}
static size_t put_wifi(uint8_t *buf, size_t cap, size_t off) {
    static const uint8_t v[] = { (uint8_t)-61, 6, 'h','o','m','e','\0',
                                 '1','0','.','0','0','.','7' };
    assert(sl2_tlv_put(buf, cap, &off, SL2_TLV_WIFI_INFO, v, sizeof v));
    return off;
}

static hvac_state_t fresh(void) { hvac_state_t s; memset(&s, 0, sizeof s); return s; }

static void test_present_sets(void) {
    hvac_state_t s = fresh();
    uint8_t buf[64];
    size_t n = put_outside(buf, sizeof buf, 0, 305);
    n = put_runtime(buf, sizeof buf, n, 1234);
    n = put_batt(buf, sizeof buf, n, 88);
    sl2_info_apply(&s, buf, n, false, 0);
    assert(s.outside_valid && s.outside_temp == 31);
    assert(s.runtime_valid && s.runtime_h == 1234);
    assert(s.sensor_batt_pct == 88);
}

static void test_absent_expires(void) {
    hvac_state_t s = fresh();
    uint8_t buf[64];
    size_t n = put_outside(buf, sizeof buf, 0, 305);
    n = put_runtime(buf, sizeof buf, n, 1234);
    n = put_batt(buf, sizeof buf, n, 88);
    sl2_info_apply(&s, buf, n, false, 0);

    n = put_fw(buf, sizeof buf, 0);
    sl2_info_apply(&s, buf, n, false, 0);
    assert(!s.outside_valid);
    assert(!s.runtime_valid);
    assert(s.sensor_batt_pct == -1);
}

static void test_empty_stream_expires(void) {
    hvac_state_t s = fresh();
    uint8_t buf[64];
    size_t n = put_outside(buf, sizeof buf, 0, 305);
    sl2_info_apply(&s, buf, n, false, 0);
    sl2_info_apply(&s, buf, 0, false, 0);
    assert(!s.outside_valid);
}

static void test_identity_persists(void) {
    hvac_state_t s = fresh();
    uint8_t buf[96];
    size_t n = put_wifi(buf, sizeof buf, 0);
    n = put_fw(buf, sizeof buf, n);
    sl2_info_apply(&s, buf, n, false, 0);
    assert(s.diag_seen);
    assert(strcmp(s.wifi_ssid, "home") == 0);
    assert(strcmp(s.unit_fw, "2026.6.1") == 0);
    assert(s.unit_wifi_ch == 6);

    n = put_outside(buf, sizeof buf, 0, 305);
    sl2_info_apply(&s, buf, n, false, 0);
    assert(s.diag_seen);
    assert(strcmp(s.wifi_ssid, "home") == 0);
    assert(strcmp(s.unit_fw, "2026.6.1") == 0);
}

static void test_reappears(void) {
    hvac_state_t s = fresh();
    uint8_t buf[64];
    size_t n = put_outside(buf, sizeof buf, 0, 305);
    sl2_info_apply(&s, buf, n, false, 0);
    sl2_info_apply(&s, buf, 0, false, 0);
    assert(!s.outside_valid);
    n = put_outside(buf, sizeof buf, 0, 280);
    sl2_info_apply(&s, buf, n, false, 0);
    assert(s.outside_valid && s.outside_temp == 28);
}

static void test_room_src_applies(void) {
    hvac_state_t s; memset(&s, 0, sizeof s);
    uint8_t buf[16]; size_t off = 0;
    const uint8_t v[2] = { SL2_ROOMSRC_LINK, SL2_ROOMST_OK };
    assert(sl2_tlv_put(buf, sizeof buf, &off, SL2_TLV_ROOM_SRC, v, 2));

    sl2_info_apply(&s, buf, off, false, 0);
    assert(s.room_src == SL2_ROOMSRC_LINK);
    assert(s.room_src_status == SL2_ROOMST_OK);
}

static void test_room_src_carries_stale_status(void) {
    hvac_state_t s; memset(&s, 0, sizeof s);
    uint8_t buf[16]; size_t off = 0;
    const uint8_t v[2] = { SL2_ROOMSRC_LINK, SL2_ROOMST_STALE };
    assert(sl2_tlv_put(buf, sizeof buf, &off, SL2_TLV_ROOM_SRC, v, 2));

    sl2_info_apply(&s, buf, off, false, 0);
    assert(s.room_src == SL2_ROOMSRC_LINK);
    assert(s.room_src_status == SL2_ROOMST_STALE);
}

static void test_room_src_expires_by_presence(void) {
    hvac_state_t s; memset(&s, 0, sizeof s);
    uint8_t buf[16]; size_t off = 0;
    const uint8_t v[2] = { SL2_ROOMSRC_LINK, SL2_ROOMST_OK };
    assert(sl2_tlv_put(buf, sizeof buf, &off, SL2_TLV_ROOM_SRC, v, 2));
    sl2_info_apply(&s, buf, off, false, 0);
    assert(s.room_src == SL2_ROOMSRC_LINK);

    sl2_info_apply(&s, buf, 0, false, 0);
    assert(s.room_src == SL2_ROOMSRC_INTERNAL);
    assert(s.room_src_status == SL2_ROOMST_OK);
}

static void test_room_src_short_tlv_ignored(void) {
    hvac_state_t s; memset(&s, 0, sizeof s);
    s.room_src = SL2_ROOMSRC_BLE;
    uint8_t buf[16]; size_t off = 0;
    const uint8_t v[1] = { SL2_ROOMSRC_LINK };
    assert(sl2_tlv_put(buf, sizeof buf, &off, SL2_TLV_ROOM_SRC, v, 1));
    sl2_info_apply(&s, buf, off, false, 0);
    assert(s.room_src == SL2_ROOMSRC_INTERNAL);
}

int main(void) {
    test_present_sets();
    test_absent_expires();
    test_empty_stream_expires();
    test_identity_persists();
    test_reappears();
    test_room_src_applies();
    test_room_src_carries_stale_status();
    test_room_src_expires_by_presence();
    test_room_src_short_tlv_ignored();
    printf("sl2_info_apply: all tests passed\n");
    return 0;
}
