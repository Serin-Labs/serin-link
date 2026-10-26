/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 * Compiled after the real edit callback and packet builder by the Python runner.
 */
enum field { TEMPERATURE, BAND, FAN };

static void choose_value(enum field field, bool requested) {
    switch (field) {
    case TEMPERATURE: display.set_dd = requested ? 210 : 200; break;
    case BAND:
        display.set_lo_dd = requested ? 200 : 190;
        display.set_hi_dd = requested ? 260 : 250;
        break;
    case FAN: display.fan_pct = requested ? 60 : 20; break;
    }
}

/* Controller state enters the production hold helpers, then becomes the UI
 * snapshot used by the real local-edit callback on its next invocation. */
static void receive_value(enum field field, bool requested) {
    choose_value(field, requested);
    ctrl_from(&display);
    switch (field) {
    case TEMPERATURE:
        display.set_dd = edit_hold_echo(&s_set_hold, display.set_dd, clock_ms, SET_HOLD_MS);
        break;
    case BAND:
        edit_hold2_echo(&s_band_hold, &display.set_lo_dd, &display.set_hi_dd,
                        clock_ms, SET_HOLD_MS);
        break;
    case FAN:
        display.fan_pct = edit_hold_echo(&s_fan_hold, display.fan_pct, clock_ms, SET_HOLD_MS);
        break;
    }
    s_last_sent = display;
}

static void expect_packet(enum field field, bool flush) {
    struct sl2_cmd_pkt p;
    assert(pending_edit_tx(&p, clock_ms, flush));
    assert(p.type == SL2_PKT_CMD && p.version == SL2_PROTO_VERSION);
    switch (field) {
    case TEMPERATURE:
        assert(p.mask == SL2_CM_TEMP && p.set_dc == 210);
        break;
    case BAND:
        assert(p.mask == SL2_CM_TEMP_BAND && p.set_low_dc == 200 && p.set_high_dc == 260);
        break;
    case FAN:
        assert(p.mask == SL2_CM_FAN && p.fan == 60);
        break;
    }
    /* Repeated polling/zone flushing must not send an unchanged held value. */
    assert(!pending_edit_tx(&p, clock_ms + 1, false));
    assert(!pending_edit_tx(&p, clock_ms + 1, true));
}

/* Celsius display value 21.0 as the default Fahrenheit table shows it (690). */
static int to_f(int dd) { return ui_dd_from_dc(ui_dd_to_dc(dd, false, 1), true, 1); }

/* The UI changes presentation while client holds remain canonical. A return
 * to the controller target before TX must still cancel the unsent edit. */
static int units_flip(enum field field, bool back, const char *name) {
    struct sl2_cmd_pkt p;
    uint32_t debounce = field == FAN ? FAN_DEBOUNCE_MS : EDIT_DEBOUNCE_MS;
    s_z[0].caps.ftab_id = 1;
    clock_ms = 1000;
    if (!back) {
        choose_value(field, true);
        espnow_client_on_local_edit();
        clock_ms += debounce / 2;
    }
    espnow_client_on_units_edit(true);
    assert(unit_posts == 1 && s_use_f);
    display.use_f = true;
    display.set_dd = to_f(display.set_dd);
    display.set_lo_dd = to_f(display.set_lo_dd);
    display.set_hi_dd = to_f(display.set_hi_dd);

    if (back) {
        int *slot = field == TEMPERATURE ? &display.set_dd
                  : field == BAND ? &display.set_lo_dd : NULL;
        if (slot) *slot += 10; else display.fan_pct = 60;
        espnow_client_on_local_edit();
        if (slot) *slot -= 10; else display.fan_pct = 20;
        espnow_client_on_local_edit();
        assert(!edits_pending());
        assert(!pending_edit_tx(&p, clock_ms + debounce, true));
        printf("PASS client edits: %s/units_back\n", name);
        return 0;
    }

    /* STATE in Fahrenheit still reports the old value: the hold masks it
     * with the new-unit value the user chose. */
    switch (field) {
    case TEMPERATURE:
        display.set_dd = to_f(edit_hold_echo(&s_set_hold, 200, clock_ms, SET_HOLD_MS));
        assert(display.set_dd == to_f(210));
        break;
    case BAND:
        display.set_lo_dd = 190; display.set_hi_dd = 250;
        edit_hold2_echo(&s_band_hold, &display.set_lo_dd, &display.set_hi_dd,
                        clock_ms, SET_HOLD_MS);
        display.set_lo_dd = to_f(display.set_lo_dd);
        display.set_hi_dd = to_f(display.set_hi_dd);
        assert(display.set_lo_dd == to_f(200) && display.set_hi_dd == to_f(260));
        break;
    case FAN:
        display.fan_pct = edit_hold_echo(&s_fan_hold, 20, clock_ms, SET_HOLD_MS);
        assert(display.fan_pct == 60);
        break;
    }
    clock_ms += debounce;
    expect_packet(field, false);

    /* The controller confirms in Fahrenheit, which ends the hold. */
    int lo = 200, hi = 260;
    edit_hold_echo(&s_set_hold, 210, clock_ms, SET_HOLD_MS);
    edit_hold2_echo(&s_band_hold, &lo, &hi, clock_ms, SET_HOLD_MS);
    edit_hold_echo(&s_fan_hold, 60, clock_ms, SET_HOLD_MS);
    assert(!edits_pending());
    printf("PASS client edits: %s/units_flip\n", name);
    return 0;
}

/* Celsius to Fahrenheit and straight back, as a knob wiggle on the Units
 * option does, before any STATE. With the generic table 21.0 C becomes 70 F,
 * then 21.1 C, so converting twice would drift. A round trip must restore
 * every hold, sent marker, and controller value exactly. */
static int units_round_trip(enum field field, const char *name) {
    s_z[0].caps.ftab_id = 0;
    clock_ms = 1000;
    choose_value(field, true);
    espnow_client_on_local_edit();
    const edit_hold_t set = s_set_hold;
    const edit_hold2_t band = s_band_hold;
    const edit_hold_t fan = s_fan_hold;
    const int sent = s_tx_sent_temp, sent_lo = s_tx_sent_lo, sent_hi = s_tx_sent_hi;
    const int ctrl = s_ctrl_set_dc, ctrl_lo = s_ctrl_lo_dc, ctrl_hi = s_ctrl_hi_dc;
    espnow_client_on_units_edit(true);
    espnow_client_on_units_edit(false);
    assert(unit_posts == 2 && !s_use_f);
    assert(s_set_hold.on == set.on && s_set_hold.val == set.val && !s_set_hold_use_f);
    assert(s_band_hold.on == band.on && s_band_hold.lo == band.lo &&
           s_band_hold.hi == band.hi && !s_band_hold_use_f);
    assert(s_fan_hold.on == fan.on && s_fan_hold.val == fan.val);
    assert(s_tx_sent_temp == sent && s_tx_sent_lo == sent_lo && s_tx_sent_hi == sent_hi);
    assert(s_ctrl_set_dc == ctrl && s_ctrl_lo_dc == ctrl_lo && s_ctrl_hi_dc == ctrl_hi);
    clock_ms += field == FAN ? FAN_DEBOUNCE_MS : EDIT_DEBOUNCE_MS;
    expect_packet(field, false);
    printf("PASS client edits: %s/units_round_trip\n", name);
    return 0;
}

/* Units in one zone cannot alter another zone's canonical controller target. */
static int units_zone_switch(const char *name) {
    s_z[0].caps.ftab_id = s_z[1].caps.ftab_id = 0;
    s_z[1].synced = true;
    s_ctrl_set_dc = 210;
    espnow_client_on_units_edit(true);
    assert(s_ctrl_set_dc == 210);
    s_active = 1;                        /* what do_zone_switch leaves behind */
    s_ctrl_set_dc = 230;
    espnow_client_on_units_edit(false);
    assert(s_ctrl_set_dc == 230);
    printf("PASS client edits: %s/units_zone_switch\n", name);
    return 0;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    enum field field = !strcmp(argv[1], "temperature") ? TEMPERATURE
                     : !strcmp(argv[1], "band") ? BAND : FAN;
    uint32_t debounce = field == FAN ? FAN_DEBOUNCE_MS : EDIT_DEBOUNCE_MS;
    bool turn_back = !strcmp(argv[2], "back_within_debounce") ||
                     !strcmp(argv[2], "state_then_back") ||
                     !strcmp(argv[2], "replay_then_back") ||
                     !strcmp(argv[2], "back_after_tx");
    s_z[0].caps = s_z[1].caps = (ui_caps_t){ .set_min_dc=160, .set_max_dc=305, .set_step_dc=5 };
    s_z[0].synced = true;
    display.mode = field == BAND ? MODE_HEAT_COOL : MODE_HEAT;
    display.band_valid = field == BAND;
    display.set_dd = 200;
    display.set_lo_dd = 190;
    display.set_hi_dd = 250;
    display.fan_pct = 20;
    s_last_sent = display;
    ctrl_from(&display);
    s_unit_hold = (edit_hold_t){0};
    if (!strcmp(argv[2], "units_flip") || !strcmp(argv[2], "units_back"))
        return units_flip(field, !strcmp(argv[2], "units_back"), argv[1]);
    if (!strcmp(argv[2], "units_round_trip")) return units_round_trip(field, argv[1]);
    if (!strcmp(argv[2], "units_zone_switch")) return units_zone_switch(argv[1]);

    clock_ms = 1000;
    choose_value(field, true);
    espnow_client_on_local_edit();
    struct sl2_cmd_pkt p;

    if (turn_back) {
        /* The user overshoots, then turns back to the value the controller
         * already holds. */
        if (!strcmp(argv[2], "back_within_debounce") ||
            !strcmp(argv[2], "state_then_back") ||
            !strcmp(argv[2], "replay_then_back")) {
            clock_ms += debounce / 2;
            if (!strcmp(argv[2], "replay_then_back")) {
                /* Controller epoch moves mid-debounce: the overshoot is
                 * replayed, so the turn-back must send a correction. */
                resend_held_edits(0);
                assert(replays == 1);
                choose_value(field, false);
                espnow_client_on_local_edit();
                assert(pending_edit_tx(&p, clock_ms + debounce, false));
                printf("PASS client edits: %s/%s\n", argv[1], argv[2]);
                return 0;
            }
            if (!strcmp(argv[2], "state_then_back")) {
                /* Periodic STATE still reports the old value but is masked
                 * through the hold, so s_last_sent now carries the overshoot. */
                receive_value(field, false);
                assert(edits_pending());
            }
            choose_value(field, false);
            espnow_client_on_local_edit();
            assert(!pending_edit_tx(&p, clock_ms + debounce, false));
            assert(!pending_edit_tx(&p, clock_ms + debounce, true));
            assert(!edits_pending());
        } else {
            clock_ms += debounce;
            expect_packet(field, false);
            clock_ms += 10;
            choose_value(field, false);
            espnow_client_on_local_edit();
            assert(!pending_edit_tx(&p, clock_ms + debounce - 1, false));
            clock_ms += debounce;
            assert(pending_edit_tx(&p, clock_ms, false));
            switch (field) {
            case TEMPERATURE: assert(p.mask == SL2_CM_TEMP && p.set_dc == 200); break;
            case BAND: assert(p.mask == SL2_CM_TEMP_BAND && p.set_low_dc == 190 &&
                              p.set_high_dc == 250); break;
            case FAN: assert(p.mask == SL2_CM_FAN && p.fan == 20); break;
            }
        }
        printf("PASS client edits: %s/%s\n", argv[1], argv[2]);
        return 0;
    }
    assert(!pending_edit_tx(&p, clock_ms + debounce - 1, false));
    clock_ms += debounce;
    expect_packet(field, false);

    /* A duplicate callback within this same edit must not produce a duplicate
     * command, even though deferred edits have not changed s_last_sent yet. */
    clock_ms += 10;
    espnow_client_on_local_edit();
    assert(!pending_edit_tx(&p, clock_ms + debounce, false));

    if (!strcmp(argv[2], "confirmed")) {
        ++clock_ms;
        receive_value(field, true);
    } else if (!strcmp(argv[2], "expired")) {
        clock_ms += SET_HOLD_MS + 1;
        receive_value(field, false);
    } else {
        edit_hold_drop(&s_set_hold);
        edit_hold2_drop(&s_band_hold);
        edit_hold_drop(&s_fan_hold);
    }
    assert(!s_set_hold.on && !s_band_hold.on && !s_fan_hold.on);

    /* An app/remote changes the target, then the user returns to the previous
     * numeric value. This is a new command even though it was sent before. */
    ++clock_ms;
    receive_value(field, false);
    ++clock_ms;
    choose_value(field, true);
    espnow_client_on_local_edit();
    assert(!pending_edit_tx(&p, clock_ms + debounce - 1, false));
    clock_ms += debounce;
    expect_packet(field, false);

    /* Leaving the zone before the debounce expires still flushes a new edit. */
    ++clock_ms;
    receive_value(field, true);
    ++clock_ms;
    receive_value(field, false);
    ++clock_ms;
    choose_value(field, true);
    espnow_client_on_local_edit();
    expect_packet(field, true);
    printf("PASS client edits: %s/%s\n", argv[1], argv[2]);
    return 0;
}
