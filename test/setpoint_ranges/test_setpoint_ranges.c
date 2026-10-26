/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
/* Production CMD paths with deterministic radio and LVGL-lock boundaries. */
#include <stdio.h>
#include <string.h>
#include "ui_dd.h"
#include "edit_hold.h"
typedef int hvac_mode_t;
typedef int vvane_t;
typedef int hvane_t;
typedef struct { int16_t set_min_dc, set_max_dc; uint8_t set_step_dc, ftab_id, vane_h, band_min_gap_dc; } ui_caps_t;
typedef struct { ui_caps_t caps; struct { bool use_f; } st; bool synced, canonical_valid; int16_t set_dc, low_dc, high_dc; } zone_t;
static zone_t s_z[1];
static int s_zn = 1;
static bool s_use_f;
static uint32_t fake_now = 1000;
static uint32_t now_ms(void) { return fake_now; }
static void sweep_backoff_reset(void) {}
typedef struct { int set_dd, set_lo_dd, set_hi_dd, mode, fan_pct, vvane, hvane; bool band_valid, use_f; } hvac_state_t;
static hvac_state_t local_state, s_last_sent;
static int local_set_dc, local_low_dc, local_high_dc;
static void ui_get_setpoints_dc(int *set, int *low, int *high) {
    *set = local_set_dc; *low = local_low_dc; *high = local_high_dc;
}
static void ui_get_state(hvac_state_t *out) { *out = local_state; }
static void ui_set_state(const hvac_state_t *in) { local_state = *in; }
static void ui_set_caps(const ui_caps_t *in) { (void)in; }
static void ui_set_state_canonical(const hvac_state_t *in, int set, int low, int high) {
    if (set != SL2_DC_NA) local_set_dc = set;
    if (low != SL2_DC_NA) local_low_dc = low;
    if (high != SL2_DC_NA) local_high_dc = high;
    ui_set_state(in);
}
static int s_active;
static int s_ctrl_set_dc, s_ctrl_lo_dc, s_ctrl_hi_dc, s_ctrl_fan;
static const char *TAG = "test";
static ui_caps_t s_caps;
static bool s_units_c;
#define SERIN_ARC_SWEEP 270.0f
static edit_hold_t s_set_hold, s_fan_hold, s_mode_hold, s_vv_hold, s_hv_hold, s_unit_hold;
static edit_hold2_t s_band_hold;
static bool s_set_hold_use_f, s_band_hold_use_f;
static int s_tx_sent_temp = -1000, s_tx_sent_lo = -1000, s_tx_sent_hi = -1000, s_tx_sent_fan = -1000;
#define EDIT_DEBOUNCE_MS 1000
#define FAN_DEBOUNCE_MS 300
#define SET_HOLD_MS 3000
#define ESP_LOGW(tag, ...) ((void)(tag))
static int lvgl_port_lock(int timeout) { (void)timeout; return 1; }
static void lvgl_port_unlock(void) {}
static int mode_to_sl2(int v) { return v; }
static int vvane_to_sl2(int v) { return v; }
static int hvane_to_sl2(int v, int caps) { (void)caps; return v; }
static struct sl2_cmd_pkt replay;
static int replay_count;
static void cmd_tx(int zi, struct sl2_cmd_pkt *c) { (void)zi; replay = *c; ++replay_count; }
static void cmd_post(int zi, struct sl2_cmd_pkt *c) { cmd_tx(zi, c); }
#include "commands.inc"
static int checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static void reset(int min, int max, int step, int ftab) {
    s_z[0].caps = (ui_caps_t){ min, max, step, ftab, 0, 0 };
    s_set_hold = s_fan_hold = s_mode_hold = s_vv_hold = s_hv_hold = s_unit_hold = (edit_hold_t){0};
    s_band_hold = (edit_hold2_t){0};
    s_tx_sent_temp = s_tx_sent_lo = s_tx_sent_hi = s_tx_sent_fan = -1000;
    s_set_hold_use_f = s_band_hold_use_f = true;
    replay_count = 0;
    s_ctrl_set_dc = s_ctrl_lo_dc = s_ctrl_hi_dc = s_ctrl_fan = 0;
}
static void note_temp(int dd, uint32_t now) {
    const ui_caps_t *c = &s_z[0].caps;
    edit_hold_note(&s_set_hold, ui_setpoint_dd_to_dc(dd, s_set_hold_use_f, c->ftab_id,
        c->set_min_dc, c->set_max_dc, c->set_step_dc), now);
}
static void note_band(int lo, int hi, uint32_t now) {
    const ui_caps_t *c = &s_z[0].caps;
    edit_hold2_note(&s_band_hold,
        ui_setpoint_dd_to_dc(lo, s_band_hold_use_f, c->ftab_id, c->set_min_dc, c->set_max_dc, c->set_step_dc),
        ui_setpoint_dd_to_dc(hi, s_band_hold_use_f, c->ftab_id, c->set_min_dc, c->set_max_dc, c->set_step_dc), now);
}
int main(void) {
    struct sl2_cmd_pkt c;
    reset(160, 305, 5, 1);
    local_state = (hvac_state_t){ .set_dd=680, .set_lo_dd=680, .set_hi_dd=750, .use_f=true };
    local_set_dc = local_low_dc = 205; local_high_dc = 240;
    ctrl_from(&local_state);
    CHECK(s_ctrl_set_dc == 205 && s_ctrl_lo_dc == 205 && s_ctrl_hi_dc == 240);
    for (int table = 0; table <= 1; ++table) {
        reset(table ? 160 : 100, table ? 305 : 310, 5, table);
        s_z[0].synced = true;
        s_use_f = true;
        fake_now = 100;
        s_last_sent = (hvac_state_t){0};
        local_state = (hvac_state_t){
            .set_dd = ui_dd_from_dc(table ? 210 : 205, true, table),
            .set_lo_dd = ui_dd_from_dc(table ? 210 : 205, true, table),
            .set_hi_dd = ui_dd_from_dc(240, true, table)};
        // The UI owns canonical coordinates; exercise the real capture callback.
        local_set_dc = local_low_dc = table ? 210 : 205;
        local_high_dc = 240;
        espnow_client_on_local_edit();
        s_z[0].caps = (ui_caps_t){160, 300, 5, !table, 0, 0};
        refresh_caps_ui(&s_z[0].caps);
        CHECK(local_state.set_dd == ui_dd_from_dc(table ? 210 : 205, true, !table));
        CHECK(local_state.set_lo_dd == ui_dd_from_dc(table ? 210 : 205, true, !table));
        // A later fan movement cannot reinterpret the remapped temperature.
        local_state.fan_pct = 55;
        espnow_client_on_local_edit();
        edit_hold_drop(&s_fan_hold);
        CHECK(pending_edit_tx(&c, 1100, false));
        CHECK(c.mask == (SL2_CM_TEMP | SL2_CM_TEMP_BAND));
        CHECK(c.set_dc == (table ? 210 : 205) && c.set_low_dc == (table ? 210 : 205) && c.set_high_dc == 240);
        resend_held_edits(0);
        CHECK(replay.set_dc == (table ? 210 : 205) && replay.set_low_dc == (table ? 210 : 205) && replay.set_high_dc == 240);
        // The production STATE path compares canonical echoes even when units
        // and mapping have changed; stale echoes keep the same selected target.
        struct sl2_state_pkt state = { .set_dc = 200, .set_low_dc = 200, .set_high_dc = 230 };
        hvac_state_t rendered = temperature_echo(&state, false, 1200);
        CHECK(rendered.set_dd == (table ? 210 : 205));
        CHECK(rendered.set_lo_dd == (table ? 210 : 205) && rendered.set_hi_dd == 240);
        // New rails and precision apply after canonical intent is recovered.
        s_z[0].caps.set_min_dc = 215;
        s_z[0].caps.set_step_dc = 10;
        resend_held_edits(0);
        CHECK(replay.set_dc == 215 && replay.set_low_dc == 215 && replay.set_high_dc == 245);
        state.set_dc = state.set_low_dc = table ? 210 : 205;
        state.set_high_dc = 240;
        temperature_echo(&state, true, 1300);
        CHECK(!s_set_hold.on && !s_band_hold.on);
    }
    // A new movement of one band edge must not reinterpret the held inactive
    // edge when that edge currently occupies a native-table hole.
    reset(100, 310, 5, 0);
    s_z[0].synced = true;
    s_use_f = true;
    s_last_sent = (hvac_state_t){0};
    local_state = (hvac_state_t){ .set_lo_dd = 689, .set_hi_dd = 752 };
    local_low_dc = 205; local_high_dc = 240;
    espnow_client_on_local_edit();
    s_z[0].caps.ftab_id = 1;
    refresh_caps_ui(&s_z[0].caps);
    local_state.set_hi_dd = 760; // User moves only the high edge to24.5C.
    local_high_dc = 245;
    espnow_client_on_local_edit();
    resend_held_edits(0);
    CHECK(replay.set_low_dc == 205 && replay.set_high_dc == 245);
    // Reversing the moved edge before any STATE echo is still a new edit.
    local_state.set_hi_dd = 750;
    local_high_dc = 240;
    espnow_client_on_local_edit();
    resend_held_edits(0);
    CHECK(replay.set_low_dc == 205 && replay.set_high_dc == 240);
    reset(100, 310, 5, 0);
    note_temp(880, 100);
    CHECK(!pending_edit_tx(&c, 1099, false));
    CHECK(pending_edit_tx(&c, 1100, false));
    CHECK(c.mask == SL2_CM_TEMP && c.set_dc == 310);
    CHECK(!pending_edit_tx(&c, 1200, false));
    resend_held_edits(0);
    CHECK(replay_count == 1 && replay.set_dc == 310);
    reset(100, 305, 5, 0);
    note_band(500, 870, 100);
    CHECK(pending_edit_tx(&c, 1100, false));
    CHECK(c.mask == SL2_CM_TEMP_BAND && c.set_low_dc == 100 && c.set_high_dc == 305);
    resend_held_edits(0);
    CHECK(replay.set_low_dc == 100 && replay.set_high_dc == 305);
    CHECK(ui_dd_from_dc(310, true, 0) == 878);
    CHECK(ui_dd_from_dc(225, true, 0) == 725);
    CHECK(ui_dd_from_dc(230, true, 0) == 734);
    CHECK(ui_setpoint_dd_to_dc(880, true, 0, 100, 310, 5) == 310);
    CHECK(ui_setpoint_dd_to_dc(870, true, 0, 100, 305, 5) == 305);
    CHECK(ui_setpoint_dd_to_dc(500, true, 0, 100, 310, 5) == 100);
    CHECK(ui_setpoint_dd_to_dc(725, true, 0, 225, 230, 5) == 225);
    CHECK(ui_setpoint_dd_to_dc(734, true, 0, 225, 230, 5) == 230);
    CHECK(ui_setpoint_dd_to_dc(725, true, 0, 100, 310, 10) == 230);
    CHECK(ui_setpoint_dd_to_dc(213, false, 0, 205, 235, 10) == 215);
    CHECK(ui_setpoint_dd_to_dc(880, true, 1, 160, 305, 5) == 305);
    CHECK(ui_setpoint_dd_to_dc(720, true, 1, 160, 305, 5) == 225);
    CHECK(ui_setpoint_dd_to_dc(730, true, 1, 160, 305, 10) == 230);
    CHECK(ui_setpoint_dd_to_dc(500, true, 1, 100, 310, 5) == 100);
    CHECK(ui_setpoint_dd_to_dc(880, true, 1, 100, 310, 5) == 310);
    /* Generic measured-value conversions and the native table stay unchanged. */
    CHECK(sl2_dc_to_display(210, true, 0) == 70);
    CHECK(sl2_dc_to_display(210, true, 1) == 69);
    CHECK(sl2_display_to_dc(88, true, 0) == 311);
    CHECK(sl2_display_to_dc(72, true, 1) == 225);
    for (int dc = 100; dc <= 310; dc += 5)
        CHECK(ui_setpoint_dd_to_dc(ui_dd_from_dc(dc, true, 0), true, 0,
                                   100, 310, 5) == dc);
    for (int f = 61; f <= 88; ++f)
        CHECK(ui_setpoint_dd_to_dc(f * 10, true, 1, 160, 305, 5) ==
              sl2_display_to_dc(f, true, 1));
    for (int table = 0; table <= 1; ++table) {
        for (int step = 5; step <= 10; step += 5) {
            reset(100, 310, step, table);
            note_temp(500, 100);
            CHECK(pending_edit_tx(&c, 1100, false) && c.set_dc == 100);
            /* CAPS refresh while an edit is held: use CURRENT rails on both
             * settle and replay, still with the unit captured at edit time. */
            note_temp(880, 1200);
            s_z[0].caps.set_max_dc = 300;
            CHECK(!pending_edit_tx(&c, 2199, false));
            CHECK(pending_edit_tx(&c, 2200, false) && c.set_dc == 300);
            resend_held_edits(0);
            CHECK(replay.set_dc == 300);
            reset(160, 300, step, table);
            s_set_hold_use_f = false;
            note_temp(225, 100);
            CHECK(pending_edit_tx(&c, 101, true));
            CHECK(c.set_dc == (step == 5 ? 225 : 230));
            resend_held_edits(0);
            CHECK(replay.set_dc == c.set_dc);
            reset(160, 300, step, table);
            int lo = ui_dd_from_dc(220, true, table);
            int hi = ui_dd_from_dc(240, true, table);
            note_band(lo, hi, 100);
            CHECK(!pending_edit_tx(&c, 1099, false));
            CHECK(pending_edit_tx(&c, 1100, false));
            CHECK(c.set_low_dc == 220 && c.set_high_dc == 240);
            CHECK(c.set_high_dc - c.set_low_dc >= 10);
            resend_held_edits(0);
            CHECK(replay.set_low_dc == 220 && replay.set_high_dc == 240);
        }
    }
    /* Native missing half-degree: ambiguous F is read-only, never 190 for 195.
     * A C hold retains canonical 195 even when Units alone changes to F. */
    s_caps = (ui_caps_t){190, 195, 5, 1, 0, 0};
    s_units_c = false;
    CHECK(dial_range_collapsed());
    CHECK(isfinite(dial_temp_t(670)) && dial_temp_t(670) == 0);
    CHECK(isfinite(dial_deg_per_step()) && dial_deg_per_step() > 0);
    s_units_c = true;
    CHECK(!dial_range_collapsed());
    CHECK(dial_temp_t(190) == 0 && dial_temp_t(195) == 1);
    reset(190, 195, 5, 1);
    note_temp(670, 100);
    CHECK(!pending_edit_tx(&c, 1100, false));
    resend_held_edits(0);
    CHECK(replay_count == 0);
    reset(190, 195, 5, 1);
    note_band(670, 670, 100);
    CHECK(!pending_edit_tx(&c, 1100, false));
    resend_held_edits(0);
    CHECK(replay_count == 0);
    edit_hold_note(&s_fan_hold, 55, 100);
    CHECK(pending_edit_tx(&c, 1100, false) && c.mask == SL2_CM_FAN && c.fan == 55);
    resend_held_edits(0);
    CHECK(replay.mask == SL2_CM_FAN && replay.fan == 55);
    reset(190, 195, 5, 1);
    s_z[0].synced = true;
    s_set_hold_use_f = false;
    note_temp(195, 100);
    espnow_client_on_units_edit(true);
    CHECK(replay.mask == SL2_CM_UNITS && replay.use_f == 1);
    CHECK(s_set_hold.val == 195 && !s_set_hold_use_f);
    CHECK(pending_edit_tx(&c, 1100, false) && c.set_dc == 195);
    resend_held_edits(0);
    CHECK(replay.set_dc == 195);
    espnow_client_on_units_edit(false);
    CHECK(replay.mask == SL2_CM_UNITS && replay.use_f == 0);
    CHECK(s_set_hold.val == 195 && !s_set_hold_use_f);
    s_caps = (ui_caps_t){160, 300, 10, 1, 0, 0};
    s_units_c = false;
    CHECK(step_setpoint_dd(730, -1) == 710);
    CHECK(step_setpoint_dd(710, 1) == 730);
    CHECK(step_setpoint_dd(730, -2) == 690);
    CHECK(step_setpoint_dd(730, 2) == 770);
    CHECK(step_setpoint_dd(730, -100) == 610);
    CHECK(step_setpoint_dd(730, 100) == 870);
    s_caps = (ui_caps_t){160, 305, 5, 1, 0, 0};
    for (int f = 61; f < 88; ++f) CHECK(step_setpoint_dd(f * 10, 1) == (f + 1) * 10);
    for (int f = 88; f > 61; --f) CHECK(step_setpoint_dd(f * 10, -1) == (f - 1) * 10);
    s_caps = (ui_caps_t){100, 305, 10, 0, 0, 0};
    CHECK(step_setpoint_dd(869, -1) == 860);  /* exact off-grid top stop -> next lower grid point */
    CHECK(step_setpoint_dd(860, 1) == 869);
    /* Current rails/precision/gap must be checked as a pair, on both send
     * paths. Suppression keeps intent; it must not block other fields or mark
     * an untransmitted band sent. CAPS can later make that intent usable. */
    const struct { int min, step, gap, low, high; } changed_caps[] = {
        {230, 5, 20, 205, 240},  /* narrowing makes the clamped pair too short */
        {160, 10, 25, 205, 230}, /* precision alone reduces a valid 2.5C gap */
        {160, 5, 25, 205, 225},  /* the advertised gap itself increases */
    };
    for (int table = 0; table <= 1; ++table) {
        for (unsigned i = 0; i < sizeof changed_caps / sizeof changed_caps[0]; ++i) {
            reset(160, 310, 5, table);
            s_band_hold_use_f = false;
            s_z[0].caps.band_min_gap_dc = i == 1 ? 25 : 20;
            note_band(changed_caps[i].low, changed_caps[i].high, 100);
            s_z[0].caps.set_min_dc = changed_caps[i].min;
            s_z[0].caps.set_step_dc = changed_caps[i].step;
            s_z[0].caps.band_min_gap_dc = changed_caps[i].gap;
            CHECK(!pending_edit_tx(&c, 1100, false));
            CHECK(!(c.mask & SL2_CM_TEMP_BAND));
            CHECK(s_tx_sent_lo == -1000 && s_tx_sent_hi == -1000);
            resend_held_edits(0);
            CHECK(replay_count == 0);
            CHECK(s_tx_sent_lo == -1000 && s_tx_sent_hi == -1000);
            CHECK(s_band_hold.on && s_band_hold.lo == changed_caps[i].low &&
                  s_band_hold.hi == changed_caps[i].high);
            edit_hold_note(&s_fan_hold, 55, 100);
            CHECK(pending_edit_tx(&c, 1100, false));
            CHECK(c.mask == SL2_CM_FAN && c.fan == 55);
            resend_held_edits(0);
            CHECK(replay_count == 1 && replay.mask == SL2_CM_FAN);
            edit_hold_drop(&s_fan_hold);
            /* No synthesized opposite edge: when CAPS permits the retained
             * coordinates again, settle and replay emit those exact values. */
            s_z[0].caps.set_min_dc = 160;
            s_z[0].caps.set_step_dc = 5;
            s_z[0].caps.band_min_gap_dc = 20;
            CHECK(pending_edit_tx(&c, 1100, false));
            CHECK(c.mask == SL2_CM_TEMP_BAND && c.set_low_dc == changed_caps[i].low &&
                  c.set_high_dc == changed_caps[i].high);
            resend_held_edits(0);
            CHECK(replay_count == 2 && replay.mask == SL2_CM_TEMP_BAND &&
                  replay.set_low_dc == changed_caps[i].low && replay.set_high_dc == changed_caps[i].high);
        }
    }
    /* Other controllers can advertise a smaller gap: do not hard-code 2C. */
    reset(160, 310, 5, 0);
    s_band_hold_use_f = false;
    note_band(230, 240, 100);
    s_z[0].caps.band_min_gap_dc = 10;
    CHECK(pending_edit_tx(&c, 1100, false) && c.mask == SL2_CM_TEMP_BAND &&
          c.set_low_dc == 230 && c.set_high_dc == 240);
    resend_held_edits(0);
    CHECK(replay_count == 1 && replay.set_low_dc == 230 && replay.set_high_dc == 240);
    printf("%d/%d setpoint range checks passed\n", checks - failures, checks);
    return failures != 0;
}
