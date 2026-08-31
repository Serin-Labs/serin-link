/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_widgets.h"
#include "ui_rim.h"
#include "icons.h"
#include "prefs.h"
#include "sl2_proto.h"
#include <stdio.h>
#include <string.h>

typedef enum { ADV_UNITS, ADV_LOCK, ADV_IDLE, ADV_NIGHT, ADV_MOTION,
               ADV_HEATPUMP, ADV_RESET, ADV_BACK, ADV_N } adv_row_t;
static const char *ADV_NAME[ADV_N] = { "Units", "Child Lock", "Idle screen", "Night", "Motion",
                                      "Heat Pump", "Reset Link", "Back" };

static const uint32_t ADV_COL[ADV_N] = {
    COL_ADV_UNITS, COL_ADV_LOCK, COL_ADV_IDLE, COL_ADV_NIGHT, COL_ADV_MOTION,
    COL_ITEM_GREEN, COL_ALERT_NOLINK, COL_BACK
};
static const serin_icon_t *const ADV_ICON[ADV_N] = {
    &set_units, &set_lock, &set_idle, &set_night, &set_motion,
    &set_system_chip, &set_reset, &nav_back
};

_Static_assert(ADV_N <= RIM_MAX, "advanced rows exceed the rim widget");

static int s_adv_sel = ADV_UNITS;
static bool s_adv_editing = false;

static rim_t ADVR;
static struct { lv_obj_t *panel, *name, *val, *meta, *hint, *holdArc,
                         *uRow, *uF, *uC,
                         *lRow, *lOff, *lOn,
                         *iRow, *iG, *iO,
                         *nRow, *nOff, *nOn,
                         *mRow, *mOff, *mOn; } AV;

static void adv_node_click_cb(lv_event_t *e);

lv_obj_t *advanced_hold_arc(void) { return AV.holdArc; }

float advanced_row_angle(void) { return rim_stop_angle(ADV_RESET, ADV_N, ADV_BACK); }

bool advanced_on_reset_row(void) { return s_adv_sel == ADV_RESET; }

int  ui_adv_sel(void)            { return s_adv_sel; }
bool ui_adv_editing_units(void)  { return s_adv_editing && s_adv_sel == ADV_UNITS; }

static void adv_toggle_pad(lv_obj_t *h)
{
    lv_obj_set_style_pad_left(h, SERIN_PX(14), 0);
    lv_obj_set_style_pad_right(h, SERIN_PX(14), 0);
    lv_obj_set_style_pad_top(h, SERIN_PX(6), 0);
    lv_obj_set_style_pad_bottom(h, SERIN_PX(6), 0);
    lv_obj_set_style_radius(h, SERIN_PX(20), 0);
}

static bool adv_half_second(lv_obj_t *first, lv_obj_t *second)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return false;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t fa, sa;
    lv_obj_get_coords(first, &fa);
    lv_obj_get_coords(second, &sa);
    return p.x > (fa.x2 + sa.x1) / 2;
}

static void adv_opt_press_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    lv_obj_t *a = (row == AV.uRow) ? AV.uF : (row == AV.lRow) ? AV.lOff :
                  (row == AV.iRow) ? AV.iG : (row == AV.nRow) ? AV.nOff : AV.mOff;
    lv_obj_t *b = (row == AV.uRow) ? AV.uC : (row == AV.lRow) ? AV.lOn  :
                  (row == AV.iRow) ? AV.iO : (row == AV.nRow) ? AV.nOn  : AV.mOn;
    lv_event_code_t c = lv_event_get_code(e);
    bool up = (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST);
    bool second = !up && adv_half_second(a, b);
    lv_obj_set_state(a, LV_STATE_PRESSED, !up && !second);
    lv_obj_set_state(b, LV_STATE_PRESSED, !up &&  second);
}

static void lock_set(bool on)
{
    if (on == s_lock.enabled) return;
    if (on) lock_init(&s_lock, true);
    else    lock_disarm(&s_lock);
    prefs_set_u8("lk_en", on ? 1 : 0);
    prefs_erase("lk_min");
    prefs_erase("lk_max");
}

static void adv_opt_tap_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    s_adv_editing = true;
    if (row == AV.uRow) {
        bool nc = adv_half_second(AV.uF, AV.uC);
        if (nc != s_units_c) {
            s_units_c = nc;
            notify_units(!s_units_c);
        }
    } else if (row == AV.lRow) {
        lock_set(adv_half_second(AV.lOff, AV.lOn));
    } else if (row == AV.iRow) {
        ui_idle_off_set(adv_half_second(AV.iG, AV.iO));
    } else if (row == AV.nRow) {
        ui_night_en_set(adv_half_second(AV.nOff, AV.nOn));
    } else {
        ui_anim_en_set(adv_half_second(AV.mOff, AV.mOn));
    }
    refresh_advanced();
}

static lv_obj_t *adv_opt_row(lv_obj_t *col, lv_obj_t **first, const char *ftxt,
                             lv_obj_t **second, const char *stxt)
{
    lv_obj_t *row = mkrow(col, SERIN_PX(6));
    *first  = mklabel(row, FONT_UNITS, COL_WHITE, ftxt);
    *second = mklabel(row, FONT_UNITS, COL_GREY,  stxt);
    adv_toggle_pad(*first);
    adv_toggle_pad(*second);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, adv_opt_tap_cb, LV_EVENT_CLICKED, NULL);
    press_ack(*first,  ACK_OPA_UNITS);
    press_ack(*second, ACK_OPA_UNITS);
    lv_obj_add_event_cb(row, adv_opt_press_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(row, adv_opt_press_cb, LV_EVENT_PRESSING,   NULL);
    lv_obj_add_event_cb(row, adv_opt_press_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(row, adv_opt_press_cb, LV_EVENT_PRESS_LOST, NULL);
    return row;
}

void build_advanced(void)
{
    lv_obj_t *p = AV.panel = s_panel[SCREEN_ADVANCED] = mkpanel(COL_FACE, false);

    float ang[ADV_N];
    for (int i = 0; i < ADV_N; i++)
        ang[i] = rim_stop_angle(i, ADV_N, ADV_BACK);

    rim_cfg_t cfg = {
        .n = ADV_N, .ang = ang, .col = ADV_COL, .glyph = ADV_ICON,
        .half_deg = 360.0f / ADV_N / 2.0f - 8.0f,
        .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 45, .disp_sel = 60, .node_px = 100, .on_tap = adv_node_click_cb,
        .no_ind = true,
    };
    rim_build(&ADVR, p, &cfg);

    lv_obj_t *c = mkcol(p, SERIN_PX(300), 12, SERIN_PX(20));
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, SERIN_PX(176));

    AV.name = mklabel(c, FONT_SET_NAME, COL_ADV_UNITS, "Units");
    AV.val  = mklabel(c, FONT_DIAL_PILL, COL_WHITE, "");
    AV.meta = mklabel(c, FONT_STATUS, COL_DIM, "");
    lv_obj_add_flag(AV.meta, LV_OBJ_FLAG_HIDDEN);

    AV.uRow = adv_opt_row(c, &AV.uF, "\xC2\xB0" "F", &AV.uC, "\xC2\xB0" "C");
    lv_obj_add_flag(AV.uRow, LV_OBJ_FLAG_HIDDEN);
    AV.lRow = adv_opt_row(c, &AV.lOff, "Off", &AV.lOn, "On");
    lv_obj_add_flag(AV.lRow, LV_OBJ_FLAG_HIDDEN);
    AV.iRow = adv_opt_row(c, &AV.iG, "Glance", &AV.iO, "Off");
    lv_obj_add_flag(AV.iRow, LV_OBJ_FLAG_HIDDEN);
    AV.nRow = adv_opt_row(c, &AV.nOff, "Off", &AV.nOn, "On");
    lv_obj_add_flag(AV.nRow, LV_OBJ_FLAG_HIDDEN);
    AV.mRow = adv_opt_row(c, &AV.mOff, "Off", &AV.mOn, "On");
    lv_obj_add_flag(AV.mRow, LV_OBJ_FLAG_HIDDEN);

    AV.hint = mklabel(c, FONT_HINT, COL_GREY, HINT_TURN_TAP);
    lv_obj_set_style_text_align(AV.hint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *ha = AV.holdArc = rim_arc(p, ang[ADV_RESET], SERIN_IND_HALF_DEG,
                                       SERIN_PX(SERIN_RIM_W_SEL), COL_ALERT_NOLINK);
    lv_obj_add_flag(ha, LV_OBJ_FLAG_HIDDEN);

}

void advanced_destroy(void) { memset(&AV, 0, sizeof AV); s_adv_editing = false; }

void refresh_advanced(void)
{
    if (!AV.panel) return;

    rim_select(&ADVR, s_adv_sel, ADV_COL[s_adv_sel]);

    lv_label_set_text(AV.name, ADV_NAME[s_adv_sel]);
    lv_obj_set_style_text_color(AV.name, lv_color_hex(ADV_COL[s_adv_sel]), 0);

    lv_obj_add_flag(AV.uRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.lRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.iRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.nRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.mRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_WHITE), 0);
    lv_obj_add_flag(AV.meta, LV_OBJ_FLAG_HIDDEN);

    switch (s_adv_sel) {
    case ADV_UNITS: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.uRow, LV_OBJ_FLAG_HIDDEN);
        bool cc = s_units_c;
        lv_obj_set_style_bg_opa(AV.uF, cc ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.uF, lv_color_hex(COL_ADV_UNITS), 0);
        lv_obj_set_style_text_color(AV.uF, lv_color_hex(cc ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.uC, cc ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.uC, lv_color_hex(COL_ADV_UNITS), 0);
        lv_obj_set_style_text_color(AV.uC, lv_color_hex(cc ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_IDLE: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.iRow, LV_OBJ_FLAG_HIDDEN);
        bool off = ui_idle_off_get();
        lv_obj_set_style_bg_opa(AV.iG, off ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.iG, lv_color_hex(COL_ADV_IDLE), 0);
        lv_obj_set_style_text_color(AV.iG, lv_color_hex(off ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.iO, off ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.iO, lv_color_hex(COL_ADV_IDLE), 0);
        lv_obj_set_style_text_color(AV.iO, lv_color_hex(off ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_NIGHT: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.nRow, LV_OBJ_FLAG_HIDDEN);
        bool on = ui_night_en_get();
        lv_obj_set_style_bg_opa(AV.nOff, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.nOff, lv_color_hex(COL_ADV_NIGHT), 0);
        lv_obj_set_style_text_color(AV.nOff, lv_color_hex(on ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.nOn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.nOn, lv_color_hex(COL_ADV_NIGHT), 0);
        lv_obj_set_style_text_color(AV.nOn, lv_color_hex(on ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_MOTION: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.mRow, LV_OBJ_FLAG_HIDDEN);
        bool on = ui_anim_en_get();
        lv_obj_set_style_bg_opa(AV.mOff, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.mOff, lv_color_hex(COL_ADV_MOTION), 0);
        lv_obj_set_style_text_color(AV.mOff, lv_color_hex(on ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.mOn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.mOn, lv_color_hex(COL_ADV_MOTION), 0);
        lv_obj_set_style_text_color(AV.mOn, lv_color_hex(on ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_LOCK: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.lRow, LV_OBJ_FLAG_HIDDEN);
        bool on = s_lock.enabled;
        lv_obj_set_style_bg_opa(AV.lOff, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.lOff, lv_color_hex(COL_ADV_LOCK), 0);
        lv_obj_set_style_text_color(AV.lOff, lv_color_hex(on ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.lOn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.lOn, lv_color_hex(COL_ADV_LOCK), 0);
        lv_obj_set_style_text_color(AV.lOn, lv_color_hex(on ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_HEATPUMP: {
        const hvac_state_t *s = &s_state;
        if (!s->cn105_connected) {
            lv_label_set_text(AV.val, "Not connected");
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_ALERT_NOLINK), 0);
        } else if (s->error_code) {
            char code[8], text[24];
            fault_code_str(code, sizeof code, s->error_code);
            snprintf(text, sizeof text, "Fault %s", code);
            lv_label_set_text(AV.val, text);
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_ALERT_WARN), 0);
        } else if (s->err_count) {
            lv_label_set_text_fmt(AV.val, "%u error%s", (unsigned)s->err_count,
                                  s->err_count == 1 ? "" : "s");
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_ALERT_NOLINK), 0);
        } else {
            lv_label_set_text(AV.val, "Connected");
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_GREEN), 0);
        }
        if (s->cn105_connected) {
            const char *sm = (s->sub_mode && s->sub_mode[0]) ? s->sub_mode : NULL;
            char meta[48]; size_t off = 0;
            meta[0] = '\0';
            if (sm) off += (size_t)snprintf(meta + off, sizeof meta - off, "%s", sm);
            if ((s_caps.features & SL2_FEAT_COMPRESSOR) && s->diag_seen)
                off += (size_t)snprintf(meta + off, sizeof meta - off,
                                        "%s%d Hz", off ? " \xC2\xB7 " : "", s->comp_freq_hz);
            if (s->outside_valid && off < sizeof meta)
                off += (size_t)snprintf(meta + off, sizeof meta - off,
                                        "%s%d\xC2\xB0 out", off ? " \xC2\xB7 " : "", s->outside_temp);
            if (off) {
                lv_label_set_text(AV.meta, meta);
                lv_obj_remove_flag(AV.meta, LV_OBJ_FLAG_HIDDEN);
            }
        }
        break;
    }
    default:        lv_label_set_text(AV.val, ""); break;
    }

    if (s_adv_sel == ADV_RESET)
        lv_label_set_text(AV.hint, H_HOLD("reset"));
    else if (s_adv_sel == ADV_LOCK && s_lock.enabled && !s_adv_editing)
        lv_label_set_text(AV.hint, H2(H_HOLD("unlock") " on the dial", HINT_TURN_TAP));
    else if (s_adv_sel == ADV_HEATPUMP)
        lv_label_set_text(AV.hint, zones_can_enter() ? H_PRESS("choose") : HINT_TURN_TAP);
    else
        lv_label_set_text(AV.hint, s_adv_editing ? H_PRESS("return") : HINT_TURN_TAP);
}

void advanced_turn(int delta)
{
    if (s_adv_editing && s_adv_sel == ADV_UNITS) {
        if (delta) {
            bool nc = (delta > 0);
            if (nc != s_units_c) {
                s_units_c = nc;
                notify_units(!s_units_c);
            }
        }
    } else if (s_adv_editing && s_adv_sel == ADV_LOCK) {
        if (delta) lock_set(delta > 0);
    } else if (s_adv_editing && s_adv_sel == ADV_IDLE) {
        if (delta) ui_idle_off_set(delta > 0);
    } else if (s_adv_editing && s_adv_sel == ADV_NIGHT) {
        if (delta) ui_night_en_set(delta > 0);
    } else if (s_adv_editing && s_adv_sel == ADV_MOTION) {
        if (delta) ui_anim_en_set(delta > 0);
    } else if (!s_adv_editing) {
        s_adv_sel = ((s_adv_sel + delta) % ADV_N + ADV_N) % ADV_N;
    }
    refresh_advanced();
}

void advanced_press(void)
{
    switch (s_adv_sel) {
    case ADV_UNITS: s_adv_editing = !s_adv_editing; break;
    case ADV_LOCK:  s_adv_editing = !s_adv_editing; break;
    case ADV_IDLE:  s_adv_editing = !s_adv_editing; break;
    case ADV_NIGHT: s_adv_editing = !s_adv_editing; break;
    case ADV_MOTION: s_adv_editing = !s_adv_editing; break;
    case ADV_HEATPUMP:
        if (zones_can_enter()) ui_show_screen(SCREEN_ZONES);
        return;
    case ADV_RESET: break;
    case ADV_BACK:  ui_show_screen(SCREEN_SETTINGS); return;
    default: break;
    }
    refresh_advanced();
}

static void adv_node_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_adv_sel = idx;
    s_adv_editing = false;
    if (idx == ADV_BACK) { ui_show_screen(SCREEN_SETTINGS); return; }
    refresh_advanced();
}
