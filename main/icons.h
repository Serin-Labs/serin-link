/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"

typedef struct {
    const lv_image_dsc_t *const *dsc;
    uint8_t n;
} serin_icon_t;

extern const serin_icon_t mode_heat, mode_cool, mode_auto, mode_dry, mode_off,
                          mode_fan;

extern const serin_icon_t nav_temp, nav_mode_mark, nav_gear, nav_back;

extern const serin_icon_t set_brightness, set_units, set_wifi, set_link_row,
                          set_homekit, set_system_chip, set_advanced;

extern const serin_icon_t digit_1, digit_2, digit_3, digit_4, digit_5,
                          digit_6, digit_7;

extern const serin_icon_t set_lock, set_idle, set_reset, set_night,
                          set_transitions;

extern const serin_icon_t serin_logo;

extern const serin_icon_t room_heatpump, room_link, room_sensor;

extern const serin_icon_t hero_alert, hero_hotspot, hero_batt, hero_check;

extern const serin_icon_t coach_arrow;
extern const serin_icon_t coach_turn_cw_l, coach_turn_cw_r,
                          coach_turn_ccw_l, coach_turn_ccw_r;

extern const serin_icon_t vane_v_auto, vane_v_1, vane_v_2, vane_v_3, vane_v_4,
                          vane_v_5, vane_v_swing;
extern const serin_icon_t vane_h_auto, vane_h_left, vane_h_cleft, vane_h_center,
                          vane_h_cright, vane_h_right, vane_h_swing;
