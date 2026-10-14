/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
#include "icons.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static rim_t ZR;
static int s_zone_sel;

bool zones_can_enter(void) { return s_zn_ui >= 2; }

void zones_opened(void)
{
    notify_zones_open();
}

void zones_turn(int delta)
{
    s_zone_sel += (delta > 0) ? 1 : -1;
    if (s_zone_sel < 0) s_zone_sel = 0;
    if (s_zone_sel >= ZR.n) s_zone_sel = ZR.n - 1;
    refresh_zones();
}

static struct { lv_obj_t *name, *nowNum, *deg, *modeTxt, *badge, *back; } ZP;

static uint32_t zone_badge_col(ui_zone_badge_t b) {
    switch (b) {
        case UI_ZB_ONLINE:   return COL_GREEN;
        case UI_ZB_FAULT:    return COL_ITEM_AMBER;
        case UI_ZB_STALE:    return COL_GREY;
        case UI_ZB_CHECKING: return COL_SYSTEM;
        default:             return COL_ALERT_NOLINK;
    }
}
static const char *zone_badge_txt(ui_zone_badge_t b) {
    switch (b) {
        case UI_ZB_ONLINE:   return "Online";
        case UI_ZB_FAULT:    return "Fault";
        case UI_ZB_STALE:    return "Stale";
        case UI_ZB_CHECKING: return "Checking";
        default:             return "Offline";
    }
}
void zone_disp_name(int i, char *out, size_t n) {
    if (s_zinfo[i].name[0]) snprintf(out, n, "%s", s_zinfo[i].name);
    else snprintf(out, n, "Zone %d %02X%02X", i + 1,
                  s_zinfo[i].mac[4], s_zinfo[i].mac[5]);
}

void zones_commit(void);
static void zone_node_click_cb(lv_event_t *e) {
    s_zone_sel = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    zones_commit();
}

static void zone_return_cb(lv_event_t *e)
{
    (void)e;
    ui_back();
}

static const char *zone_state_txt(const ui_zone_info_t *z)
{
    hvac_state_t s = { .mode = z->mode };
    return status_text(&s, z->status == ST_ACTIVE,
                       z->mode == MODE_OFF || z->status == ST_OFF);
}
static void zone_dd_txt(int dd, char *out, size_t n)
{
    if (dd % 10) snprintf(out, n, "%d.%d", dd / 10, abs(dd % 10));
    else         snprintf(out, n, "%d", dd / 10);
}
static bool zone_target_txt(const ui_zone_info_t *z, char *out, size_t n)
{
    out[0] = 0;
    if (!z->have_state || z->mode == MODE_OFF || z->mode == MODE_FAN) return false;
    char a[16], b[16];
    if (z->mode == MODE_HEAT_COOL && z->band_valid) {
        zone_dd_txt(z->set_lo_dd, a, sizeof a);
        zone_dd_txt(z->set_hi_dd, b, sizeof b);
        snprintf(out, n, "%s-%s\xC2\xB0", a, b);
        return true;
    }
    if (z->set_dd <= 0) return false;
    zone_dd_txt(z->set_dd, a, sizeof a);
    snprintf(out, n, "%s\xC2\xB0", a);
    return true;
}

void refresh_zones(void) {
    if (!s_panel[SCREEN_ZONES]) return;
    int nz = ZR.n - 1;
    if (s_zone_sel >= ZR.n) s_zone_sel = ZR.n - 1;
    if (s_zone_sel < 0)     s_zone_sel = 0;
    bool back = (s_zone_sel == nz);
    rim_select(&ZR, s_zone_sel,
        back ? COL_BACK : zone_badge_col(s_zinfo[s_zone_sel].badge));
    for (int i = 0; i < nz; i++) {
        rim_tint(&ZR, i, zone_badge_col(s_zinfo[i].badge));
    }
    if (back) {
        lv_label_set_text(ZP.name, "Back");
        lv_label_set_long_mode(ZP.name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(ZP.name, lv_color_hex(COL_BACK), 0);
        lv_obj_add_flag(ZP.nowNum, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ZP.deg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ZP.modeTxt, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ZP.badge, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ZP.back, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(ZP.back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ZP.nowNum, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ZP.modeTxt, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ZP.badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(ZP.name, lv_color_hex(COL_WHITE), 0);
    char nm[48]; zone_disp_name(s_zone_sel, nm, sizeof nm);
    lv_label_set_text(ZP.name, nm);
    /* The displayed zone is the focused one, so it may reveal its full name
     * after LVGL's standard scroll delay while every other zone remains in
     * the rim.  The fixed one-line box prevents it from moving the readout. */
    lv_label_set_long_mode(ZP.name, LV_LABEL_LONG_SCROLL);
    const ui_zone_info_t *z = &s_zinfo[s_zone_sel];
    lv_label_set_text_fmt(ZP.badge, "%s%s", zone_badge_txt(z->badge),
                          s_zone_sel == s_zactive ? " \xC2\xB7 Active" : "");
    lv_obj_set_style_text_color(ZP.badge, lv_color_hex(zone_badge_col(z->badge)), 0);
    if (!z->have_state) {
        lv_label_set_text(ZP.nowNum, "--");
        lv_obj_add_flag(ZP.deg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_opa(ZP.nowNum, LV_OPA_COVER, 0);
        lv_label_set_text(ZP.modeTxt, "No reading yet");
        return;
    }
    lv_label_set_text_fmt(ZP.nowNum, "%d", z->room_temp);
    lv_obj_remove_flag(ZP.deg, LV_OBJ_FLAG_HIDDEN);
    bool last = z->badge == UI_ZB_OFFLINE || z->badge == UI_ZB_STALE;
    lv_obj_set_style_text_opa(ZP.nowNum, last ? REMIND_HERO_OPA : LV_OPA_COVER, 0);
    lv_obj_set_style_text_opa(ZP.deg, last ? REMIND_HERO_OPA : LV_OPA_COVER, 0);
    /* Center the number itself; the degree suffix must not shift its axis.
     * Reposition after layout because the reading can change digit count. */
    lv_obj_update_layout(s_panel[SCREEN_ZONES]);
    lv_obj_align_to(ZP.deg, ZP.nowNum, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    const char *prefix = last ? "Last \xC2\xB7 " : "";
    char tgt[36];
    bool band = (z->mode == MODE_HEAT_COOL && z->band_valid);
    if (zone_target_txt(z, tgt, sizeof tgt))
        lv_label_set_text_fmt(ZP.modeTxt, band ? "%s%s %s" : "%s%s to %s",
                              prefix, zone_state_txt(z), tgt);
    else
        lv_label_set_text_fmt(ZP.modeTxt, "%s%s", prefix, zone_state_txt(z));
}

static const serin_icon_t *zone_digit_glyph(int i)
{
    static const serin_icon_t *const D[UI_MAX_ZONES] = {
        &digit_1, &digit_2, &digit_3, &digit_4, &digit_5, &digit_6, &digit_7,
    };
    if (i < 0) i = 0;
    if (i >= UI_MAX_ZONES) i = UI_MAX_ZONES - 1;
    return D[i];
}

void build_zones(void) {
    lv_obj_t *p = s_panel[SCREEN_ZONES] = mkpanel(COL_FACE, false);
    int n = s_zn_ui;
    if (n > UI_MAX_ZONES) n = UI_MAX_ZONES;
    if (n < 1) n = 1;
    int nn = n + 1;
    float ang[UI_MAX_ZONES + 1]; uint32_t col[UI_MAX_ZONES + 1];
    const serin_icon_t *gly[UI_MAX_ZONES + 1];
    for (int i = 0; i < n; i++) {
        ang[i] = rim_stop_angle(i, nn, n);
        col[i] = zone_badge_col(s_zinfo[i].badge);
        gly[i] = zone_digit_glyph(i);
    }
    ang[n] = rim_stop_angle(n, nn, n);
    col[n] = COL_BACK;
    gly[n] = &nav_back;
    rim_cfg_t cfg = {
        .n = nn, .ang = ang, .col = col, .glyph = gly,
        .half_deg = 180.0f / nn - 6.0f,
        .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 46, .disp_sel = 66,
        .node_px = 100,
        .on_tap = zone_node_click_cb,
        .no_ind = true,
    };
    rim_build(&ZR, p, &cfg);

    ZP.name = mkslot_label(p, FONT_SET_NAME, COL_WHITE, "", 310, SERIN_SLOT_TITLE_Y);
    ZP.modeTxt = mkslot_label(p, FONT_STATUS, COL_GREY, "", 260, SERIN_SLOT_DETAIL_Y);
    ZP.nowNum = mkslot_label(p, FONT_SSID, COL_WHITE, "", 230, SERIN_SLOT_CONTROL_Y);
    lv_obj_set_width(ZP.nowNum, LV_SIZE_CONTENT);
    ZP.deg = mklabel(p, FONT_SSID, COL_WHITE, "\xC2\xB0");
    ZP.badge = mklabel(p, FONT_HINT, COL_GREY, "");
    lv_obj_align(ZP.badge, LV_ALIGN_TOP_MID, 0, SERIN_PX(SERIN_SLOT_HINT_TOP));
    ZP.back = mkslot_action(p, "Return", zone_return_cb, NULL);
    lv_obj_add_flag(ZP.back, LV_OBJ_FLAG_HIDDEN);
    s_zone_sel = s_zactive;
}

static void zone_switch_request(int idx)
{
    if (idx < 0 || idx >= s_zn_ui || idx == s_zactive) return;
    s_zactive = idx;
    zchip_refresh();
    zchip_pulse();
    notify_zone_select(idx);
}

void zone_swipe(int step)
{
    if (s_zn_ui < 2) return;
    zone_switch_request((s_zactive + step + s_zn_ui) % s_zn_ui);
}

void zones_commit(void) {
    /* The trailing stop is Back, so it retraces; picking a zone is a commit and
     * lands on the thermostat, where the newly active zone is what you see. */
    if (s_zone_sel >= ZR.n - 1) { ui_back(); return; }
    zone_switch_request(s_zone_sel);
    ui_show_screen(SCREEN_THERMOSTAT);
}

void zones_changed(int n)
{
    if (!s_panel[SCREEN_ZONES]) return;
    if (ZR.n == n + 1) { refresh_zones(); return; }

    if (s_cur == SCREEN_ZONES && n < 2) { ui_back(); return; }
    screen_rebuild(SCREEN_ZONES);
}
