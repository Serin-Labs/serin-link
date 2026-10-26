/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
/* Use the simulator's compiled, real UI. Only radio/clock/peer boundaries are
 * stubbed; refresh_caps_ui -> ui_set_state -> edit_rails is the production path. */
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "ui_dd.h"
#include "edit_hold.h"
#include "bsp.h"
#include "sim.h"
#include "sim_time.h"
#include "prefs.h"
#include "lvgl.h"

bool sim_headless = true;
volatile bool sim_quit = false;
typedef struct { ui_caps_t caps; hvac_state_t st; bool synced, canonical_valid; int16_t set_dc, low_dc, high_dc; } zone_t;
static zone_t s_z[2];
static int s_zn = 1, s_active;
static bool s_use_f;
static hvac_state_t s_last_sent;
static int s_ctrl_set_dc, s_ctrl_lo_dc, s_ctrl_hi_dc, s_ctrl_fan;
static const char *TAG = "test";
static uint32_t now_ms(void) { return sim_time_ms(); }
static void sweep_backoff_reset(void) {}
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

static void reset_band(int low_dc, int high_dc, int from_table, int to_table, bool use_f) {
    s_set_hold = s_fan_hold = s_mode_hold = s_vv_hold = s_hv_hold = s_unit_hold = (edit_hold_t){0};
    s_band_hold = (edit_hold2_t){0};
    s_tx_sent_temp = s_tx_sent_lo = s_tx_sent_hi = s_tx_sent_fan = -1000;
    replay_count = 0;
    s_use_f = use_f;
    s_z[0].synced = true;
    s_z[0].caps = (ui_caps_t){ .modes = 0x7f, .set_min_dc = 160, .set_max_dc = 305,
        .set_step_dc = 5, .ftab_id = from_table, .band_min_gap_dc = 20 };
    ui_set_caps(&s_z[0].caps);
    hvac_state_t s;
    ui_get_state(&s);
    s.mode = MODE_HEAT_COOL;
    s.status = ST_IDLE;
    s.use_f = use_f;
    s.alert = ALERT_NONE;
    s.cn105_connected = true;
    s.band_valid = true;
    s.set_lo_dd = ui_dd_from_dc(low_dc, use_f, from_table);
    s.set_hi_dd = ui_dd_from_dc(high_dc, use_f, from_table);
    ui_set_state(&s);
    s_last_sent = s;
    s_ctrl_fan = s.fan_pct; // No unrelated pending fan movement at fixture start.
    s_ctrl_lo_dc = low_dc - 5; s_ctrl_hi_dc = high_dc - 5;
    s_last_sent.set_lo_dd = 0; // Establish the actual local edit hold.
    espnow_client_on_local_edit();
    if (!use_f) {
        // A Celsius hold survives switching to F through actual echo/publication.
        struct sl2_state_pkt stale = { .set_dc = SL2_DC_NA,
            .set_low_dc = low_dc - 5, .set_high_dc = high_dc - 5 };
        receive_temperature_state(&stale, true);
    }
    s_z[0].caps.ftab_id = to_table;
    refresh_caps_ui(&s_z[0].caps); // Calls the real ui_set_state, never a stub.
}

static void select_edge(bool high) {
    sim_tap(high ? 310 : 150, 233);
    sim_time_wait(120);
}
static void check_band(int low, int high) {
    struct sl2_cmd_pkt c;
    CHECK(pending_edit_tx(&c, now_ms() + 1000, false));
    printf("settled band: %d/%d, expected %d/%d\n", c.set_low_dc, c.set_high_dc, low, high);
    CHECK(c.set_low_dc == low && c.set_high_dc == high);
    CHECK(c.set_high_dc - c.set_low_dc >= 20);
    resend_held_edits(0);
    CHECK(replay.set_low_dc == low && replay.set_high_dc == high);
}

int main(void) {
    prefs_set_bool("coach", true);
    sim_time_init(true);
    lv_init();
    lv_tick_set_cb(sim_time_ms);
    bsp_display_start();
    ui_init();
    ui_set_on_edit(espnow_client_on_local_edit);
    ui_set_link_state(true, true, (const uint8_t[]){1,2,3,4,5,6}, -42);
    bsp_input_start(ui_encoder, ui_button, ui_button_long, ui_button_down, ui_button_up);
    sim_time_wait(1700);

    reset_band(205, 240, 0, 1, true);
    // A sensor-only refresh uses ui_set_state without new temperature authority.
    ui_set_link_sensor(true, true, 216, 50);
    select_edge(true);
    ui_encoder(-100);
    check_band(205, 225);
    ui_encoder(1);
    check_band(205, 230);
    ui_encoder(-1);
    check_band(205, 225);

    reset_band(170, 205, 0, 1, true);
    select_edge(false);
    ui_encoder(100);
    check_band(185, 205);

    // The other missing native half-degree has the same gap risk.
    reset_band(195, 220, 0, 1, true);
    select_edge(true);
    ui_encoder(-100);
    check_band(195, 215);
    reset_band(160, 195, 0, 1, true);
    select_edge(false);
    ui_encoder(100);
    check_band(175, 195);

    // Reverse mapping and exact C holds use the same canonical rail.
    reset_band(205, 240, 1, 0, false);
    select_edge(true);
    ui_encoder(-100);
    check_band(205, 225);
    reset_band(170, 205, 1, 0, false);
    select_edge(false);
    ui_encoder(100);
    check_band(185, 205);

    reset_band(205, 240, 0, 1, true);
    struct sl2_state_pkt state = { .set_dc = SL2_DC_NA, .set_low_dc = 200, .set_high_dc = 240 };
    receive_temperature_state(&state, true); // Stale echo must keep held205.
    select_edge(true);
    ui_encoder(-100);
    check_band(205, 225);

    // A matching STATE retires the hold without discarding canonical authority.
    // The next opposite-edge edit must read the same coordinate as the UI rail.
    reset_band(205, 240, 0, 1, true);
    state.set_low_dc = 205;
    state.set_high_dc = 240;
    receive_temperature_state(&state, true);
    CHECK(!s_band_hold.on);
    select_edge(true);
    ui_encoder(-100);
    check_band(205, 225);

    reset_band(170, 205, 0, 1, true);
    state.set_low_dc = 170;
    state.set_high_dc = 205;
    receive_temperature_state(&state, true);
    CHECK(!s_band_hold.on);
    select_edge(false);
    ui_encoder(100);
    check_band(185, 205);

    reset_band(195, 220, 0, 1, true);
    state.set_low_dc = 195;
    state.set_high_dc = 220;
    receive_temperature_state(&state, true);
    CHECK(!s_band_hold.on);
    select_edge(true);
    ui_encoder(-100);
    check_band(195, 215);

    reset_band(160, 195, 0, 1, true);
    state.set_low_dc = 160;
    state.set_high_dc = 195;
    receive_temperature_state(&state, true);
    CHECK(!s_band_hold.on);
    select_edge(false);
    ui_encoder(100);
    check_band(175, 195);

    // Expiry must also end optimism and accept new canonical authority.
    reset_band(205, 240, 0, 1, true);
    sim_time_wait(SET_HOLD_MS + 1);
    state.set_low_dc = 200;
    state.set_high_dc = 240;
    receive_temperature_state(&state, true);
    CHECK(!s_band_hold.on);
    select_edge(true);
    ui_encoder(-100);
    check_band(200, 220);

    reset_band(205, 240, 0, 1, true);
    edit_hold2_drop(&s_band_hold); // No outstanding edit: STATE is authoritative.
    hvac_state_t before, after;
    ui_get_state(&before);
    receive_temperature_state(&state, true); // New20C has the SAME native68F label.
    ui_get_state(&after);
    CHECK(before.set_lo_dd == after.set_lo_dd);
    select_edge(true);
    ui_encoder(-100);
    check_band(200, 220); // The new canonical truth replaces205 despite equal labels.

    reset_band(205, 240, 0, 1, true);
    s_z[0].caps.set_step_dc = 10;
    refresh_caps_ui(&s_z[0].caps);
    select_edge(true);
    ui_encoder(-100);
    check_band(210, 230); // Current precision revalidates the inactive edge too.

    // A warm zone restore must carry canonical native-table holes alongside
    // its cached labels, including STATE received while the zone is inactive.
    reset_band(205, 240, 0, 1, true);
    state.set_dc = SL2_DC_NA; state.set_low_dc = 205; state.set_high_dc = 240;
    receive_temperature_state(&state, true);
    ui_get_state(&s_z[0].st);
    s_z[1].caps = s_z[0].caps;
    s_z[1].st = s_z[0].st;
    state.set_low_dc = 195; state.set_high_dc = 220;
    cache_temperature_state(1, &state);
    CHECK(s_z[1].low_dc == 195 && s_z[1].high_dc == 220);
    s_active = 1;
    restore_zone_ui(1, &s_z[1].st);
    CHECK(s_ctrl_lo_dc == 195 && s_ctrl_hi_dc == 220);
    int read_set, read_low, read_high;
    ui_get_setpoints_dc(&read_set, &read_low, &read_high);
    CHECK(read_low == 195 && read_high == 220);
    s_active = 0;
    restore_zone_ui(0, &s_z[0].st);
    ui_get_setpoints_dc(&read_set, &read_low, &read_high);
    CHECK(read_low == 205 && read_high == 240);
    CHECK(s_ctrl_lo_dc == 205 && s_ctrl_hi_dc == 240);
    select_edge(true);
    ui_encoder(-100);
    check_band(205, 225);

    // CAPS-only precision/range changes cannot turn a later fan movement into
    // an unrelated temperature edit after the previous hold has retired.
    reset_band(205, 240, 0, 0, true);
    state.set_low_dc = 205; state.set_high_dc = 240;
    receive_temperature_state(&state, true);
    CHECK(!s_band_hold.on);
    s_z[0].caps.set_min_dc = 210;
    refresh_caps_ui(&s_z[0].caps);
    hvac_state_t fan_edit;
    ui_get_state(&fan_edit);
    fan_edit.fan_pct = 55;
    ui_set_state(&fan_edit);
    espnow_client_on_local_edit();
    struct sl2_cmd_pkt fan_cmd;
    CHECK(pending_edit_tx(&fan_cmd, now_ms() + 1000, false));
    CHECK(fan_cmd.mask == SL2_CM_FAN && !s_band_hold.on && !s_set_hold.on);

    // A units-only change after CAPS normalization must also update the
    // presentation comparison baseline before a later unrelated fan edit.
    ui_get_state(&fan_edit);
    ui_get_setpoints_dc(&read_set, &read_low, &read_high);
    fan_edit.use_f = false;
    fan_edit.set_dd = read_set;
    fan_edit.set_lo_dd = read_low;
    fan_edit.set_hi_dd = read_high;
    ui_set_state_canonical(&fan_edit, read_set, read_low, read_high);
    espnow_client_on_units_edit(false);
    fan_edit.fan_pct = 60;
    ui_set_state(&fan_edit);
    espnow_client_on_local_edit();
    CHECK(pending_edit_tx(&fan_cmd, now_ms() + 1000, false));
    CHECK(fan_cmd.mask == SL2_CM_FAN && !s_band_hold.on && !s_set_hold.on);

    // Exercise production CAPS publication and real UI canonical ownership
    // through narrowing, coarser precision, and an increased advertised gap.
    const struct { int min, step, gap, low, high; } changed_caps[] = {
        {230, 5, 20, 205, 240},
        {160, 10, 25, 205, 230},
        {160, 5, 25, 205, 225},
    };
    for (int table = 0; table <= 1; ++table) {
        for (unsigned i = 0; i < sizeof changed_caps / sizeof changed_caps[0]; ++i) {
            reset_band(changed_caps[i].low, changed_caps[i].high, table, table, false);
            s_z[0].caps.band_min_gap_dc = i == 1 ? 25 : 20;
            refresh_caps_ui(&s_z[0].caps);
            s_z[0].caps.set_min_dc = changed_caps[i].min;
            s_z[0].caps.set_step_dc = changed_caps[i].step;
            s_z[0].caps.band_min_gap_dc = changed_caps[i].gap;
            refresh_caps_ui(&s_z[0].caps);
            struct sl2_cmd_pkt c;
            CHECK(!pending_edit_tx(&c, now_ms() + 1000, false));
            CHECK(!(c.mask & SL2_CM_TEMP_BAND));
            CHECK(s_tx_sent_lo == -1000 && s_tx_sent_hi == -1000);
            resend_held_edits(0);
            CHECK(replay_count == 0);
            CHECK(s_band_hold.on && s_band_hold.lo == changed_caps[i].low &&
                  s_band_hold.hi == changed_caps[i].high);
            hvac_state_t fan;
            ui_get_state(&fan);
            fan.fan_pct = fan.fan_pct == 55 ? 60 : 55;
            ui_set_state(&fan);
            espnow_client_on_local_edit();
            CHECK(pending_edit_tx(&c, now_ms() + 1000, false));
            CHECK(c.mask == SL2_CM_FAN && c.fan == fan.fan_pct);
            resend_held_edits(0);
            CHECK(replay_count == 1 && replay.mask == SL2_CM_FAN);
            CHECK(s_band_hold.lo == changed_caps[i].low && s_band_hold.hi == changed_caps[i].high);
            edit_hold_drop(&s_fan_hold);
            s_z[0].caps.set_min_dc = 160;
            s_z[0].caps.set_step_dc = 5;
            s_z[0].caps.band_min_gap_dc = 20;
            refresh_caps_ui(&s_z[0].caps);
            CHECK(pending_edit_tx(&c, now_ms() + 1000, false));
            CHECK(c.mask == SL2_CM_TEMP_BAND && c.set_low_dc == changed_caps[i].low &&
                  c.set_high_dc == changed_caps[i].high);
            resend_held_edits(0);
            CHECK(replay_count == 2 && replay.mask == SL2_CM_TEMP_BAND &&
                  replay.set_low_dc == changed_caps[i].low && replay.set_high_dc == changed_caps[i].high);
        }
    }
    reset_band(230, 240, 0, 0, false);
    s_z[0].caps.band_min_gap_dc = 10;
    refresh_caps_ui(&s_z[0].caps);
    CHECK(pending_edit_tx(&fan_cmd, now_ms() + 1000, false));
    CHECK(fan_cmd.mask == SL2_CM_TEMP_BAND && fan_cmd.set_low_dc == 230 && fan_cmd.set_high_dc == 240);
    resend_held_edits(0);
    CHECK(replay_count == 1 && replay.set_low_dc == 230 && replay.set_high_dc == 240);

    printf("%d/%d canonical band gap checks passed\n", checks-failures, checks);
    return failures != 0;
}
