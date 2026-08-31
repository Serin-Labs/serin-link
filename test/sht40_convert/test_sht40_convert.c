/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sht40_convert.h"

static void test_crc8_datasheet_vector(void) {
    const uint8_t v[2] = { 0xBE, 0xEF };
    assert(sht40_crc8(v, 2) == 0x92);
}

static void test_crc8_detects_single_bit_flip(void) {
    const uint8_t good[2] = { 0xBE, 0xEF };
    uint8_t bad[2] = { 0xBE, 0xEE };
    assert(sht40_crc8(good, 2) != sht40_crc8(bad, 2));
}

static void test_temp_endpoints(void) {
    assert(sht40_raw_to_dc(0)     == -450);
    assert(sht40_raw_to_dc(65535) ==  1300);
}

static void test_temp_midscale_rounds(void) {
    assert(sht40_raw_to_dc(32768) == 425);
}

static void test_temp_room_value(void) {
    assert(sht40_raw_to_dc(25091) == 220);
}

static void test_temp_below_zero_is_negative(void) {
    assert(sht40_raw_to_dc(10000) == -183);
}

static void test_rh_clamps_low(void) {
    assert(sht40_raw_to_rh_pct(0) == 0);
}

static void test_rh_clamps_high(void) {
    assert(sht40_raw_to_rh_pct(65535) == 100);
}

static void test_rh_midscale(void) {
    assert(sht40_raw_to_rh_pct(32768) == 57);
}

static void test_rh_gain_identity(void) {
    assert(sht40_rh_apply_gain(0, 1000)     == 0);
    assert(sht40_rh_apply_gain(4757, 1000)  == 4757);
    assert(sht40_rh_apply_gain(10000, 1000) == 10000);
}

static void test_rh_gain_measured_value(void) {
    assert(sht40_rh_apply_gain(4757, 1176) == 5594);
}

static void test_rh_gain_clamps_at_saturation(void) {
    assert(sht40_rh_apply_gain(9500, 1176)  == 10000);
    assert(sht40_rh_apply_gain(10000, 1176) == 10000);
}

static void test_rh_gain_is_proportional_not_additive(void) {
    const uint16_t lo = sht40_rh_apply_gain(3000, 1176);
    const uint16_t hi = sht40_rh_apply_gain(6000, 1176);
    assert(hi - 6000 > (lo - 3000) * 3 / 2);
    assert(lo == 3528 && hi == 7056);
}

static void test_rh_gain_rounds_not_truncates(void) {
    assert(sht40_rh_apply_gain(1, 1600) == 2);
}

static void test_parse_accepts_good_frame(void) {
    uint8_t rx[6];
    rx[0] = 0x62; rx[1] = 0x03; rx[2] = sht40_crc8(rx, 2);
    rx[3] = 0x80; rx[4] = 0x00; rx[5] = sht40_crc8(rx + 3, 2);
    int16_t cc; uint16_t rh_cc;
    assert(sht40_parse(rx, &cc, &rh_cc));
    assert(cc == 2200);
    assert(rh_cc == 5650);
}

static void test_parse_emits_centi(void) {
    uint8_t rx[6];
    rx[0] = 0x62; rx[1] = 0x03; rx[2] = sht40_crc8(rx, 2);
    rx[3] = 0x80; rx[4] = 0x00; rx[5] = sht40_crc8(rx + 3, 2);
    int16_t cc; uint16_t rh_cc;
    assert(sht40_parse(rx, &cc, &rh_cc));
    assert(cc == 2200);
    assert(rh_cc == 5650);
}

static void test_parse_rejects_bad_temp_crc(void) {
    uint8_t rx[6];
    rx[0] = 0x62; rx[1] = 0x03; rx[2] = (uint8_t)(sht40_crc8(rx, 2) ^ 0xFF);
    rx[3] = 0x80; rx[4] = 0x00; rx[5] = sht40_crc8(rx + 3, 2);
    int16_t cc; uint16_t rh_cc;
    assert(!sht40_parse(rx, &cc, &rh_cc));
}

static void test_parse_rejects_bad_rh_crc(void) {
    uint8_t rx[6];
    rx[0] = 0x62; rx[1] = 0x03; rx[2] = sht40_crc8(rx, 2);
    rx[3] = 0x80; rx[4] = 0x00; rx[5] = (uint8_t)(sht40_crc8(rx + 3, 2) ^ 0xFF);
    int16_t cc; uint16_t rh_cc;
    assert(!sht40_parse(rx, &cc, &rh_cc));
}

static void test_parse_rejects_all_zero_frame(void) {
    uint8_t rx[6] = { 0 };
    int16_t cc; uint16_t rh_cc;
    assert(!sht40_parse(rx, &cc, &rh_cc));
}

static void test_offset_glance_is_the_baseline(void) {
    assert(sht40_offset_eff(230, false, 60) == 230);
}

static void test_offset_idle_off_subtracts_delta(void) {
    assert(sht40_offset_eff(230, true, 60) == 170);
}

static void test_offset_idle_off_is_never_larger(void) {
    for (int16_t d = 1; d <= 2000; d = (int16_t)(d + 70))
        for (int16_t base = -2000; base <= 2000; base = (int16_t)(base + 100))
            assert(sht40_offset_eff(base, true, d) < sht40_offset_eff(base, false, d));
}

static void test_offset_zero_delta_makes_modes_agree(void) {
    assert(sht40_offset_eff(230, true, 0) == sht40_offset_eff(230, false, 0));
}

static void test_offset_composes_past_the_console_range_cc(void) {
    assert(sht40_offset_eff(-2000, true, 2000) == -4000);
}

static void test_offset_deci_to_centi_migration(void) {
    assert(sht40_offset_dc_to_cc(21)   == 210);
    assert(sht40_offset_dc_to_cc(0)    == 0);
    assert(sht40_offset_dc_to_cc(-200) == -2000);
    assert(sht40_offset_dc_to_cc(200)  == 2000);
}

static void test_temp_cc_endpoints(void) {
    assert(sht40_raw_to_cc(0)     == -4500);
    assert(sht40_raw_to_cc(65535) == 13000);
}

static void test_temp_cc_midscale(void) {
    assert(sht40_raw_to_cc(32768) == 4250);
}

static void test_temp_cc_room_value(void) {
    assert(sht40_raw_to_cc(25091) == 2200);
}

static void test_temp_cc_below_zero_rounds_away_from_zero(void) {
    assert(sht40_raw_to_cc(10000) == -1830);
}

static void test_temp_cc_tracks_dc_within_one_deci(void) {
    for (unsigned long raw = 0; raw <= 65535UL; raw++) {
        const int16_t cc = sht40_raw_to_cc((uint16_t)raw);
        const int16_t dc = sht40_raw_to_dc((uint16_t)raw);
        const int16_t from_cc = (int16_t)((cc >= 0) ? (cc + 5) / 10 : (cc - 5) / 10);
        const int diff = (int)from_cc - (int)dc;
        assert(diff >= -1 && diff <= 1);
    }
}

static void test_rh_cc_clamps_low(void) {
    assert(sht40_raw_to_rh_ccpct(0) == 0);
}

static void test_rh_cc_clamps_high(void) {
    assert(sht40_raw_to_rh_ccpct(65535) == 10000);
}

static void test_rh_cc_midscale(void) {
    assert(sht40_raw_to_rh_ccpct(32768) == 5650);
}

static void test_rh_cc_tracks_pct_within_one_percent(void) {
    for (unsigned long raw = 0; raw <= 65535UL; raw++) {
        const uint16_t cc = sht40_raw_to_rh_ccpct((uint16_t)raw);
        const uint8_t  pc = sht40_raw_to_rh_pct((uint16_t)raw);
        const int diff = (int)((cc + 50) / 100) - (int)pc;
        assert(diff >= -1 && diff <= 1);
    }
}

static void test_airflow_blend_snaps_at_or_past_tau(void) {
    assert(sht40_airflow_step_q8(0, 2300, 600000, 600000) == 2300 << 8);
    assert(sht40_airflow_step_q8(0, 2300, 900000, 600000) == 2300 << 8);
    assert(sht40_airflow_step_q8(12345, 0, 600000, 0) == 0);
}

static void test_airflow_blend_first_step_fraction(void) {
    assert(sht40_airflow_step_q8(0, 2300, 10000, 600000) == 9813);
}

static void test_airflow_blend_efold_at_tau(void) {
    int32_t q = 0;
    for (int i = 0; i < 60; i++) q = sht40_airflow_step_q8(q, 23, 10000, 600000);
    const int16_t cc = sht40_q8_to_cc(q);
    assert(cc >= 14 && cc <= 15);
}

static void test_airflow_blend_step_is_tau_symmetric(void) {
    int32_t q = 23 << 8;
    for (int i = 0; i < 60; i++) q = sht40_airflow_step_q8(q, 0, 10000, 600000);
    const int16_t cc = sht40_q8_to_cc(q);
    assert(cc >= 8 && cc <= 9);
}

static void test_airflow_blend_converges_fully(void) {
    int32_t q = 0;
    for (int i = 0; i < 300; i++) q = sht40_airflow_step_q8(q, 23, 10000, 600000);
    assert(sht40_q8_to_cc(q) == 23);
    for (int i = 0; i < 300; i++) q = sht40_airflow_step_q8(q, 0, 10000, 600000);
    assert(sht40_q8_to_cc(q) == 0);
}

static void test_airflow_q8_rounds_to_nearest_cc(void) {
    assert(sht40_q8_to_cc(383) == 1);
    assert(sht40_q8_to_cc(384) == 2);
    assert(sht40_q8_to_cc(-383) == -1);
    assert(sht40_q8_to_cc(23 << 8) == 23);
}

static void test_q8_from_cc_round_trips_negative(void) {
    assert(sht40_q8_to_cc(sht40_q8_from_cc(-1830)) == -1830);
    assert(sht40_q8_to_cc(sht40_q8_from_cc(2200))  == 2200);
    assert(sht40_q8_from_cc(-1) == -256);
}

static void test_lpf_step_toward_negative_target_lands(void) {
    assert(sht40_lpf_step_q8(0, -1000, 300000, 300000) == -1000 * 256);
    assert(sht40_lpf_step_q8(0, -1000, 10000, 300000) == -8533);
}

static void test_lpf_is_the_airflow_primitive(void) {
    assert(sht40_airflow_step_q8(0, 2300, 10000, 600000) ==
           sht40_lpf_step_q8(0, 2300, 10000, 600000));
    assert(sht40_airflow_step_q8(12345, 0, 600000, 0) ==
           sht40_lpf_step_q8(12345, 0, 600000, 0));
}

static void test_temp_filter_efold_at_five_minutes(void) {
    int32_t q = sht40_q8_from_cc(2200);
    for (int i = 0; i < 30; i++) q = sht40_lpf_step_q8(q, 2300, 10000, 300000);
    const int16_t cc = sht40_q8_to_cc(q);
    assert(cc >= 2262 && cc <= 2264);
}

static void test_temp_filter_long_gap_lands_exactly(void) {
    assert(sht40_q8_to_cc(sht40_lpf_step_q8(sht40_q8_from_cc(2200), 2300,
                                            300000, 300000)) == 2300);
}

static void test_regime_step_fan_on_uses_tau_in(void) {
    assert(sht40_airflow_regime_step_q8(0, true, 23, 10000, 600000, 1500000) ==
           sht40_airflow_step_q8(0, 23, 10000, 600000));
}

static void test_regime_step_fan_off_uses_tau_out(void) {
    const int32_t start = 23 << 8;
    assert(sht40_airflow_regime_step_q8(start, false, 23, 10000, 600000, 1500000) ==
           sht40_airflow_step_q8(start, 0, 10000, 1500000));
}

static void test_regime_step_asymmetry_direction(void) {
    int32_t in_q = 0, out_q = 23 << 8;
    for (int i = 0; i < 60; i++) {
        in_q  = sht40_airflow_regime_step_q8(in_q,  true,  23, 10000, 600000, 1500000);
        out_q = sht40_airflow_regime_step_q8(out_q, false, 23, 10000, 600000, 1500000);
    }
    const int32_t in_covered  = in_q;
    const int32_t out_covered = (23 << 8) - out_q;
    assert(in_covered > out_covered);
    assert(out_q > (23 << 8) * 36 / 100);
}

static void test_offset_eff_air_composes(void) {
    assert(sht40_offset_eff_air(286, false, 0, 23 << 8) == 263);
    assert(sht40_offset_eff_air(286, false, 0, 0)       == 286);
    assert(sht40_offset_eff_air(286, false, 0, 9813)    == 286 - 38);
    assert(sht40_offset_eff_air(286, true, 50, 23 << 8) == 286 - 50 - 23);
}

static void test_offset_eff_air_matches_base_composition(void) {
    for (int16_t base = -100; base <= 400; base += 50) {
        assert(sht40_offset_eff_air(base, false, 0, 0) ==
               sht40_offset_eff(base, false, 0));
        assert(sht40_offset_eff_air(base, true, 60, 0) ==
               sht40_offset_eff(base, true, 60));
    }
}

int main(void) {
    test_crc8_datasheet_vector();
    test_crc8_detects_single_bit_flip();
    test_temp_endpoints();
    test_temp_midscale_rounds();
    test_temp_room_value();
    test_temp_below_zero_is_negative();
    test_rh_clamps_low();
    test_rh_clamps_high();
    test_rh_midscale();
    test_rh_gain_identity();
    test_rh_gain_measured_value();
    test_rh_gain_clamps_at_saturation();
    test_rh_gain_is_proportional_not_additive();
    test_rh_gain_rounds_not_truncates();
    test_parse_accepts_good_frame();
    test_parse_emits_centi();
    test_parse_rejects_bad_temp_crc();
    test_parse_rejects_bad_rh_crc();
    test_parse_rejects_all_zero_frame();
    test_offset_glance_is_the_baseline();
    test_offset_idle_off_subtracts_delta();
    test_offset_idle_off_is_never_larger();
    test_offset_zero_delta_makes_modes_agree();
    test_offset_composes_past_the_console_range_cc();
    test_offset_deci_to_centi_migration();
    test_temp_cc_endpoints();
    test_temp_cc_midscale();
    test_temp_cc_room_value();
    test_temp_cc_below_zero_rounds_away_from_zero();
    test_temp_cc_tracks_dc_within_one_deci();
    test_rh_cc_clamps_low();
    test_rh_cc_clamps_high();
    test_rh_cc_midscale();
    test_rh_cc_tracks_pct_within_one_percent();
    test_airflow_blend_snaps_at_or_past_tau();
    test_airflow_blend_first_step_fraction();
    test_airflow_blend_efold_at_tau();
    test_airflow_blend_step_is_tau_symmetric();
    test_airflow_blend_converges_fully();
    test_airflow_q8_rounds_to_nearest_cc();
    test_q8_from_cc_round_trips_negative();
    test_lpf_step_toward_negative_target_lands();
    test_lpf_is_the_airflow_primitive();
    test_temp_filter_efold_at_five_minutes();
    test_temp_filter_long_gap_lands_exactly();
    test_regime_step_fan_on_uses_tau_in();
    test_regime_step_fan_off_uses_tau_out();
    test_regime_step_asymmetry_direction();
    test_offset_eff_air_composes();
    test_offset_eff_air_matches_base_composition();
    printf("sht40_convert: all tests passed\n");
    return 0;
}
