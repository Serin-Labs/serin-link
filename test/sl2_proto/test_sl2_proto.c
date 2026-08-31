/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sl2_proto.h"

static void test_ftab1_round_trips_every_f(void) {
    for (int f = SL2_FTAB1_MIN_F; f <= SL2_FTAB1_MAX_F; f++)
        assert(sl2_ftab1_c_to_f(sl2_ftab1_f_to_c(f)) == f);
}

static void test_ftab1_known_points(void) {
    assert(sl2_ftab1_f_to_c(71) == 22.0f);
    assert(sl2_ftab1_f_to_c(72) == 22.5f);
    assert(sl2_ftab1_c_to_f(19.5f) == 67);
}

static void test_ftab1_clamps_out_of_range(void) {
    assert(sl2_ftab1_f_to_c(0)   == sl2_ftab1_f_to_c(SL2_FTAB1_MIN_F));
    assert(sl2_ftab1_f_to_c(120) == sl2_ftab1_f_to_c(SL2_FTAB1_MAX_F));
    assert(sl2_ftab1_c_to_f(5.0f)  == SL2_FTAB1_MIN_F);
    assert(sl2_ftab1_c_to_f(40.0f) == SL2_FTAB1_MAX_F);
}

static void test_display_celsius(void) {
    assert(sl2_display_to_dc(22, false, 0) == 220);
    assert(sl2_dc_to_display(220, false, 0) == 22);
    assert(sl2_dc_to_display(225, false, 0) == 23);
}

static void test_display_fahrenheit_ftab1(void) {
    assert(sl2_dc_to_display(220, true, 1) == 71);
    assert(sl2_display_to_dc(71, true, 1) == 220);
    for (int f = SL2_FTAB1_MIN_F; f <= SL2_FTAB1_MAX_F; f++)
        assert(sl2_dc_to_display(sl2_display_to_dc(f, true, 1), true, 1) == f);
}

static void test_display_fahrenheit_linear_fallback(void) {
    assert(sl2_dc_to_display(350, true, 1) == 95);
    assert(sl2_display_to_dc(72, true, 0) == 222);
}

static void test_decode_short_frame_zero_fills(void) {
    struct sl2_caps_pkt c;
    memset(&c, 0xAA, sizeof c);
    uint8_t wire[SL2_CAPS_MIN_LEN];
    memset(wire, 0x11, sizeof wire);
    sl2_decode_pkt(&c, sizeof c, wire, (int)sizeof wire);
    assert(c.type == 0x11);
    for (size_t i = 0; i < sizeof c.reserved; i++)
        assert(c.reserved[i] == 0);
}

static void test_decode_long_frame_truncates(void) {
    struct sl2_probe_pkt p;
    uint8_t wire[64];
    memset(wire, 0x22, sizeof wire);
    sl2_decode_pkt(&p, sizeof p, wire, (int)sizeof wire);
    assert(p.type == 0x22 && p.version == 0x22);
}

static void test_tlv_put_refuses_overflow(void) {
    uint8_t buf[8]; size_t off = 0;
    assert( sl2_tlv_put(buf, sizeof buf, &off, 1, "abcd", 4));
    assert(!sl2_tlv_put(buf, sizeof buf, &off, 2, "abcd", 4));
    assert(off == 6);
    assert( sl2_tlv_put(buf, sizeof buf, &off, 3, NULL, 0));
    assert(off == 8);
}

static void test_tlv_next_stops_at_truncation(void) {
    uint8_t buf[] = { 1, 2, 0xAA, 0xBB,   9, 200, 0xCC };
    size_t off = 0; uint8_t t, l; const uint8_t *v;
    assert(sl2_tlv_next(buf, sizeof buf, &off, &t, &l, &v));
    assert(t == 1 && l == 2 && v[0] == 0xAA);
    assert(!sl2_tlv_next(buf, sizeof buf, &off, &t, &l, &v));
    assert(off == 4);
}

static void test_tlv_next_empty_and_exact(void) {
    uint8_t buf[] = { 7, 0,   8, 1, 0x5A };
    size_t off = 0; uint8_t t, l; const uint8_t *v;
    assert(sl2_tlv_next(buf, sizeof buf, &off, &t, &l, &v) && t == 7 && l == 0);
    assert(sl2_tlv_next(buf, sizeof buf, &off, &t, &l, &v) && t == 8 && v[0] == 0x5A);
    assert(!sl2_tlv_next(buf, sizeof buf, &off, &t, &l, &v));
}

static void test_mac_eq(void) {
    uint8_t a[6] = {1,2,3,4,5,6}, b[6] = {1,2,3,4,5,6};
    assert(sl2_mac_eq(a, b));
    b[5] = 7;
    assert(!sl2_mac_eq(a, b));
}

int main(void) {
    test_ftab1_round_trips_every_f();
    test_ftab1_known_points();
    test_ftab1_clamps_out_of_range();
    test_display_celsius();
    test_display_fahrenheit_ftab1();
    test_display_fahrenheit_linear_fallback();
    test_decode_short_frame_zero_fills();
    test_decode_long_frame_truncates();
    test_tlv_put_refuses_overflow();
    test_tlv_next_stops_at_truncation();
    test_tlv_next_empty_and_exact();
    test_mac_eq();
    printf("sl2_proto: all tests passed\n");
    return 0;
}
