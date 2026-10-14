/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sl2_ui_map.h"

static void test_mode_round_trips(void) {
    static const hvac_mode_t all[] = { MODE_OFF, MODE_HEAT, MODE_COOL,
                                       MODE_HEAT_COOL, MODE_AUTO, MODE_DRY, MODE_FAN };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        assert(mode_from_sl2(mode_to_sl2(all[i])) == all[i]);
    assert(mode_from_sl2(0xEE) == MODE_AUTO);
}

#define CAP_NATIVE_H (SL2_VANECAP(6, false, true) | SL2_VANECAP_SPLIT_TOP)
#define CAP_LEGACY_H SL2_VANECAP(6, false, true)

static void test_vvane_round_trips(void) {
    static const vvane_t all[] = { VANE_AUTO, VANE_1, VANE_2, VANE_3,
                                   VANE_4, VANE_5, VANE_SWING };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        assert(vvane_from_sl2(vvane_to_sl2(all[i])) == all[i]);
    assert(vvane_from_sl2(0) == VANE_AUTO);
    assert(vvane_from_sl2(SL2_UI_VANE_MAX_POS + 1) == VANE_5);
    assert(vvane_from_sl2(15) == VANE_5);
    assert(vvane_from_sl2(SL2_VANE_SWING) == VANE_SWING);
}

static void test_hvane_round_trips(void) {
    static const hvane_t all[] = { HVANE_AUTO, HVANE_LEFT, HVANE_CLEFT,
                                   HVANE_CENTER, HVANE_CRIGHT, HVANE_RIGHT, HVANE_SWING };
    for (size_t i = 0; i < sizeof all / sizeof *all; i++)
        assert(hvane_from_sl2(hvane_to_sl2(all[i], CAP_NATIVE_H),
                              CAP_NATIVE_H) == all[i]);
    assert(hvane_from_sl2(0, CAP_NATIVE_H) == HVANE_CENTER);
    assert(hvane_from_sl2(15, CAP_NATIVE_H) == HVANE_RIGHT);
}

static void test_hvane_split_position(void) {
    assert(sl2_vanecap_split_pos(CAP_NATIVE_H) == 6);
    assert(sl2_vanecap_split_pos(CAP_LEGACY_H) == 6);
    assert(hvane_from_sl2(6, CAP_NATIVE_H) == HVANE_AUTO);
    assert(hvane_from_sl2(6, CAP_LEGACY_H) == HVANE_AUTO);
    assert(hvane_to_sl2(HVANE_AUTO, CAP_NATIVE_H) == 6);
    assert(hvane_to_sl2(HVANE_AUTO, CAP_LEGACY_H) == 6);

    const uint8_t five = SL2_VANECAP(5, false, true);
    assert(sl2_vanecap_split_pos(five) == 0);
    assert(hvane_from_sl2(5, five) == HVANE_RIGHT);
    assert(hvane_to_sl2(HVANE_AUTO, five) == SL2_VANE_AUTO);

    assert(sl2_vanecap_split_pos(0) == 0);
    assert(hvane_from_sl2(6, 0) == HVANE_RIGHT);

    const uint8_t four_split = SL2_VANECAP(4, false, false) | SL2_VANECAP_SPLIT_TOP;
    assert(sl2_vanecap_split_pos(four_split) == 4);
    assert(hvane_from_sl2(4, four_split) == HVANE_AUTO);
    assert(hvane_from_sl2(3, four_split) == HVANE_CENTER);
    assert(hvane_to_sl2(HVANE_AUTO, four_split) == 4);
}

static void test_vanecap_bits_are_independent(void) {
    assert(sl2_vanecap_npos(CAP_NATIVE_H) == 6);
    assert(!sl2_vanecap_has_auto(CAP_NATIVE_H));
    assert(sl2_vanecap_has_swing(CAP_NATIVE_H));
    assert(sl2_vanecap_split_top(CAP_NATIVE_H));
    assert(!sl2_vanecap_split_top(CAP_LEGACY_H));
    const uint8_t v = SL2_VANECAP(5, true, true);
    assert(sl2_vanecap_npos(v) == 5 && sl2_vanecap_has_auto(v) &&
           sl2_vanecap_has_swing(v) && !sl2_vanecap_split_top(v));
}

static void test_action_labels(void) {
    assert(action_active(SL2_ACT_HEATING) && action_active(SL2_ACT_DEFROST));
    assert(!action_active(SL2_ACT_IDLE) && !action_active(SL2_ACT_STANDBY));
    assert(strcmp(action_substate_str(SL2_ACT_DEFROST), "DEFROST") == 0);
    assert(strcmp(action_substate_str(SL2_ACT_HEATING), "") == 0);
    assert(strcmp(stage_str(0x05), "HIGH") == 0);
    assert(strcmp(stage_str(0x77), "?") == 0);
}

static void test_tlv_str2_two_strings(void) {
    char a[8], b[8];
    const uint8_t v[] = { 'x', 'y', 's', 's', 'i', 'd', 0, 'i', 'p' };
    tlv_str2(v, sizeof v, 2, a, sizeof a, b, sizeof b);
    assert(strcmp(a, "ssid") == 0 && strcmp(b, "ip") == 0);
}

static void test_tlv_str2_missing_nul_bounds_first(void) {
    char a[4], b[8];
    const uint8_t v[] = { 'a', 'b', 'c', 'd', 'e', 'f' };
    tlv_str2(v, sizeof v, 0, a, sizeof a, b, sizeof b);
    assert(strcmp(a, "abc") == 0);
    assert(b[0] == '\0');
}

static void test_tlv_str2_short_and_empty(void) {
    char a[8] = "xx", b[8] = "yy";
    tlv_str2((const uint8_t *)"", 0, 0, a, sizeof a, b, sizeof b);
    assert(a[0] == '\0' && b[0] == '\0');
    const uint8_t v[] = { 1, 2 };
    tlv_str2(v, sizeof v, 2, a, sizeof a, b, sizeof b);
    assert(a[0] == '\0' && b[0] == '\0');
}

static void test_screen_gate_decode(void) {
    assert(sl2_screen_gate_from_flags2(0) == UI_SCREEN_GATE_NONE);
    assert(sl2_screen_gate_from_flags2(SL2_SF2_SCREEN_OFF) == UI_SCREEN_GATE_NONE);
    assert(sl2_screen_gate_from_flags2(SL2_SF2_SCREEN_CTL) == UI_SCREEN_GATE_ON);
    assert(sl2_screen_gate_from_flags2(SL2_SF2_SCREEN_CTL | SL2_SF2_SCREEN_OFF)
           == UI_SCREEN_GATE_OFF);
    assert(sl2_screen_gate_from_flags2(SL2_SF2_SENSOR_BATT_LOW) == UI_SCREEN_GATE_NONE);
}

static void test_screen_gate_merge_on_wins(void) {
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_NONE, UI_SCREEN_GATE_NONE) == UI_SCREEN_GATE_NONE);
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_ON,   UI_SCREEN_GATE_OFF)  == UI_SCREEN_GATE_ON);
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_OFF,  UI_SCREEN_GATE_ON)   == UI_SCREEN_GATE_ON);
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_ON,   UI_SCREEN_GATE_NONE) == UI_SCREEN_GATE_ON);
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_OFF,  UI_SCREEN_GATE_NONE) == UI_SCREEN_GATE_OFF);
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_NONE, UI_SCREEN_GATE_OFF)  == UI_SCREEN_GATE_OFF);
    assert(sl2_screen_gate_merge(UI_SCREEN_GATE_OFF,  UI_SCREEN_GATE_OFF)  == UI_SCREEN_GATE_OFF);
}

int main(void) {
    test_mode_round_trips();
    test_vvane_round_trips();
    test_hvane_round_trips();
    test_hvane_split_position();
    test_vanecap_bits_are_independent();
    test_action_labels();
    test_tlv_str2_two_strings();
    test_tlv_str2_missing_nul_bounds_first();
    test_tlv_str2_short_and_empty();
    test_screen_gate_decode();
    test_screen_gate_merge_on_wins();
    printf("sl2_ui_map: all tests passed\n");
    return 0;
}
