/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
#include "ui_rimlist.h"
#include "ui_arim.h"
#include "icons.h"
#include "prefs.h"
#include "espnow_client.h"
#include "sl2_proto.h"
#include <stdio.h>
#include <string.h>

void homekit_qr_entry(void);
void about_details_cb(lv_event_t *e);

static rimlist_t s_set = { .ids = { 0, 1, 2, 3, 4, 5, 6, 7 }, .n = SET_N };
bool s_set_editing = false;

static bool set_keep(int i) { return !(i == SET_HOMEKIT && !(s_caps.features & SL2_FEAT_HOMEKIT)); }
static void set_list_rebuild(void) { rimlist_build(&s_set, SET_N, set_keep); }
static inline int set_id(int pos) { return rimlist_id(&s_set, pos); }
static int set_pos(int id) { return rimlist_pos(&s_set, id); }
static inline float set_ang(int id) { return rim_stop_angle(set_pos(id), s_set.n, set_pos(SET_BACK)); }
#define SET_LINK_ANG set_ang(SET_LINK)

static void set_chip_click_cb(lv_event_t *e);
static void settings_node_click_cb(lv_event_t *e);
static void wifi_row_press_cb(lv_event_t *e);
static void wifi_row_release_cb(lv_event_t *e);

static int s_set_landing = -1;

void settings_enter(void)
{
    s_set_sel = s_set_landing >= 0 ? s_set_landing : SET_BRIGHT;
    s_set_landing = -1;
    if (!set_keep(s_set_sel)) s_set_sel = SET_BRIGHT;
    s_set_editing = false;
}

void settings_caps_changed(void)
{
    if (!s_panel[SCREEN_SETTINGS]) return;
    if (!rimlist_moved(&s_set, SET_N, set_keep)) return;
    if (set_pos(s_set_sel) == 0 && s_set_sel != set_id(0)) s_set_sel = set_id(0);
    ui_hold_abort();
    screen_rebuild(SCREEN_SETTINGS);
}

void settings_land(int set_row)
{
    s_set_landing = (set_row >= 0 && set_row < SET_N) ? set_row : SET_BRIGHT;
    ui_show_screen(SCREEN_SETTINGS);
}

void settings_turn(int delta)
{
    if (s_pair_state != PAIR_UI_HIDDEN) return;
    if (s_set_editing && s_set_sel == SET_BRIGHT) {
        int b = s_brightness + delta * 5;
        if (b < 10) b = 10;
        if (b > 100) b = 100;
        s_brightness = b;
        apply_brightness();
    } else {
        s_set_sel = set_id(rimlist_wrap(s_set.n, set_pos(s_set_sel) + delta));
    }
    refresh_settings();
}

static const char *SET_NAME[SET_N] = {
    "Brightness", "Wi-Fi", "Controller", "HomeKit", "Room Sensor", "About", "Advanced", "Back"
};

static const char *set_name(int i)
{
    if (i != SET_HOMEKIT) return SET_NAME[i];
    switch (hk_protocol()) {
    case HK_PROTO_MATTER:  return "Matter";
    case HK_PROTO_HOMEKIT: return "HomeKit";
    default:               return "Home";
    }
}

const uint32_t SET_COL[SET_N] = {
    COL_ITEM_AMBER, COL_SYSTEM, COL_LINK, COL_SPLASH_LOGO, COL_ITEM_GREEN, COL_HEAT_GLOW, COL_ADVANCED, COL_BACK
};

static const serin_icon_t *const SET_ICON[SET_N] = {
    &set_brightness, &set_wifi, &set_link_row, &set_homekit, &set_units,
    &serin_logo,
    &set_advanced,
    &nav_back
};

static rim_t SETR;
static struct { lv_obj_t *name, *brTrack, *brFill, *brPct,
                         *status, *hint,
                         *wrow, *wdot, *wconn,
                         *hkcode, *hkrow, *hkpaired, *hkdot,
                         *center, *lrow, *ldot, *lconn, *lmeta, *holdArc,
                         *syrow, *sydot, *syconn, *roomSrc,
                         *abatt, *abtn, *abtnlbl; } SS;

static void settings_chip(lv_obj_t *row)
{
    lv_obj_set_style_radius(row, SERIN_PX(33), 0);
    lv_obj_set_style_bg_opa(row, OPA_FILL_NEUTRAL, 0);
    lv_obj_set_style_bg_color(row, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(row, SERIN_PX(24), 0);
    lv_obj_set_style_pad_ver(row, SERIN_PX(11), 0);
}

lv_obj_t *mkchip_btn(lv_obj_t *parent, const char *text, uint32_t txt_col,
                     uint32_t solid_bg, lv_event_cb_t cb,
                     const touch_floor_arg_t *tf)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    settings_chip(o);
    if (solid_bg) {
        lv_obj_set_style_bg_color(o, lv_color_hex(solid_bg), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, NULL);
    mklabel(o, FONT_DIAL_PILL, txt_col, text);
    touch_floor(o, tf);
    return o;
}

static void chip_status(lv_obj_t *dot, lv_obj_t *lbl, uint32_t col, const char *txt)
{
    lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, lv_color_hex(col), 0);
}

static void bright_track_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p; lv_indev_get_point(indev, &p);
    lv_area_t a; lv_obj_get_coords(SS.brTrack, &a);
    int w = a.x2 - a.x1;
    if (w <= 0) return;
    int pct = (p.x - a.x1) * 100 / w;
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    if (pct == s_brightness) return;
    s_brightness = pct;
    apply_brightness();
    lv_obj_set_width(SS.brFill, SERIN_PX(188) * pct / 100);
    lv_label_set_text_fmt(SS.brPct, "%d%%", pct);
}

static lv_obj_t *settings_status_row(lv_obj_t *parent, int set_id, const char *placeholder,
                                     const touch_floor_arg_t *tf,
                                     lv_obj_t **out_dot, lv_obj_t **out_label)
{
    lv_obj_t *r = mkrow(parent, SERIN_PX(15));
    settings_chip(r);
    lv_obj_set_user_data(r, (void *)(intptr_t)set_id);
    lv_obj_add_event_cb(r, set_chip_click_cb, LV_EVENT_CLICKED, NULL);
    press_ack(r, ACK_OPA_CHIP);
    *out_dot   = mkdot(r, SERIN_PX(14), COL_GREEN);
    *out_label = mklabel(r, FONT_DIAL_PILL, COL_GREEN, placeholder);
    touch_floor(r, tf);
    return r;
}

void build_settings(void)
{
    lv_obj_t *p = s_panel[SCREEN_SETTINGS] = mkpanel(COL_FACE, false);

    set_list_rebuild();
    int back_pos = set_pos(SET_BACK);
    float ang[SET_N]; uint32_t col[SET_N]; const serin_icon_t *gly[SET_N];
    for (int i = 0; i < s_set.n; i++) {
        int m = set_id(i);
        ang[i] = rim_stop_angle(i, s_set.n, back_pos);
        col[i] = SET_COL[m];
        gly[i] = SET_ICON[m];
    }
    rim_cfg_t cfg = {
        .n = s_set.n, .ang = ang, .col = col, .glyph = gly,
        .half_deg = 360.0f / s_set.n / 2.0f - 8.0f,
        .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 45, .disp_sel = 60, .node_px = 100, .on_tap = settings_node_click_cb,
        .no_ind = true,
    };
    rim_build(&SETR, p, &cfg);

    lv_obj_t *c = mkcol(p, SERIN_PX(340), 10, SERIN_PX(20));
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, SERIN_PX(176));

    SS.name = mklabel(c, FONT_SET_NAME, COL_ITEM_AMBER, "Brightness");

    SS.brTrack = lv_obj_create(c);
    lv_obj_remove_style_all(SS.brTrack);
    lv_obj_set_size(SS.brTrack, SERIN_PX(188), SERIN_PX(10));
    lv_obj_set_style_radius(SS.brTrack, SERIN_PX(5), 0);
    lv_obj_set_style_bg_opa(SS.brTrack, (lv_opa_t)36, 0);
    lv_obj_set_style_bg_color(SS.brTrack, lv_color_hex(COL_PURE), 0);
    lv_obj_remove_flag(SS.brTrack, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(SS.brTrack, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(SS.brTrack, bright_track_cb, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(SS.brTrack, bright_track_cb, LV_EVENT_PRESSING, NULL);
    SS.brFill = lv_obj_create(SS.brTrack);
    lv_obj_remove_style_all(SS.brFill);
    lv_obj_set_size(SS.brFill, SERIN_PX(188), SERIN_PX(10));
    lv_obj_set_style_radius(SS.brFill, SERIN_PX(5), 0);
    lv_obj_set_style_bg_opa(SS.brFill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(SS.brFill, lv_color_hex(SET_COL[SET_BRIGHT]), 0);
    lv_obj_align(SS.brFill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(SS.brFill, LV_OBJ_FLAG_CLICKABLE);

    SS.brPct = mklabel(c, FONT_BRIGHT, COL_WHITE, "100%");

    SS.status = mklabel(c, FONT_STATUS, COL_SYSTEM, "Open");

    static const touch_floor_arg_t k_wrow_tf = { 0, "SS.wrow" };
    SS.wrow = settings_status_row(c, SET_WIFI, "Connected", &k_wrow_tf, &SS.wdot, &SS.wconn);
    lv_obj_add_event_cb(SS.wrow, wifi_row_press_cb,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(SS.wrow, wifi_row_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(SS.wrow, wifi_row_release_cb, LV_EVENT_PRESS_LOST, NULL);

    SS.hkcode = mklabel(c, FONT_HK_CODE, COL_WHITE, "548-94-669");

    static const touch_floor_arg_t k_hkrow_tf = { 0, "SS.hkrow" };
    SS.hkrow = settings_status_row(c, SET_HOMEKIT, "Paired", &k_hkrow_tf,
                                   &SS.hkdot, &SS.hkpaired);

    SS.center = c;
    static const touch_floor_arg_t k_lrow_tf = { 0, "SS.lrow" };
    SS.lrow = settings_status_row(c, SET_LINK, "Connected", &k_lrow_tf, &SS.ldot, &SS.lconn);

    SS.lmeta = mklabel(c, FONT_STATUS, COL_DIM, "");

    static const touch_floor_arg_t k_syrow_tf = { 0, "SS.syrow" };
    SS.syrow = settings_status_row(c, SET_SYSTEM, "Connected", &k_syrow_tf,
                                   &SS.sydot, &SS.syconn);

    SS.roomSrc = mklabel(c, FONT_STATUS, COL_DIM, "");

    SS.abatt = mklabel(c, FONT_STATUS, COL_DIM, "");

    static const touch_floor_arg_t k_abtn_tf = { 0, "SS.abtn" };
    SS.abtn = mkchip_btn(c, "Details", COL_WHITE, 0, about_details_cb, &k_abtn_tf);
    press_ack(SS.abtn, ACK_OPA_CHIP);
    SS.abtnlbl = lv_obj_get_child(SS.abtn, 0);

    SS.hint   = mklabel(c, FONT_HINT, COL_GREY, "");
    lv_obj_set_style_text_align(SS.hint, LV_TEXT_ALIGN_CENTER, 0);

    static const touch_floor_arg_t k_brtrack_tf = { 0, "SS.brTrack" };
    touch_floor(SS.brTrack, &k_brtrack_tf);

    SS.holdArc = rim_arc(p, SET_LINK_ANG, SERIN_IND_HALF_DEG,
                         SERIN_PX(SERIN_RIM_W_SEL), COL_ALERT_NOLINK);
    lv_obj_add_flag(SS.holdArc, LV_OBJ_FLAG_HIDDEN);
}

enum { R_BRTRACK, R_BRPCT, R_STATUS, R_WROW, R_HKCODE, R_HKROW, R_LROW, R_LMETA,
       R_SYROW, R_ROOMSRC, R_ABATT, R_ABTN, R_HINT, R_N };
static uint16_t s_ss_want;
static void ss_rows_begin(void) { s_ss_want = 1u << R_HINT; }
static void ss_show(int r) { s_ss_want |=  (uint16_t)(1u << r); }
static void ss_hide(int r) { s_ss_want &= (uint16_t)~(1u << r); }
static void ss_rows_apply(void)
{
    lv_obj_t *const rows[R_N] = { SS.brTrack, SS.brPct, SS.status, SS.wrow, SS.hkcode,
                                  SS.hkrow, SS.lrow, SS.lmeta, SS.syrow,
                                  SS.roomSrc, SS.abatt, SS.abtn, SS.hint };
    for (int i = 0; i < R_N; i++) {
        if (s_ss_want & (1u << i)) lv_obj_remove_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void refresh_settings(void)
{
    rim_hide(&SETR, false);
    lv_obj_remove_flag(SS.center, LV_OBJ_FLAG_HIDDEN);

    int idx = s_set_sel;
    int pos = set_pos(idx);
    rim_select(&SETR, pos, COL_SYSTEM);

    lv_label_set_text(SS.name, set_name(idx));
    lv_obj_set_style_text_color(SS.name, lv_color_hex(SET_COL[idx]), 0);

    ss_rows_begin();

    if (idx == SET_BRIGHT) {
        ss_show(R_BRTRACK);
        ss_show(R_BRPCT);
        lv_obj_set_width(SS.brFill, SERIN_PX(188) * s_brightness / 100);
        lv_label_set_text_fmt(SS.brPct, "%d%%", s_brightness);
        lv_label_set_text(SS.hint, H_PRESS("return"));
        if (!s_set_editing) ss_hide(R_HINT);
    } else if (idx == SET_WIFI) {
        const hvac_state_t *s = &s_state;
        ss_show(R_WROW);
        const char *wtxt; uint32_t wcol;
        if (!s_link.online)         { wtxt = "Controller offline"; wcol = COL_ALERT_NOLINK; }
        else if (s->wifi_connected) { wtxt = "Connected"; wcol = COL_GREEN; }
        else                        { wtxt = "Offline";   wcol = COL_GREY; }
        chip_status(SS.wdot, SS.wconn, wcol, wtxt);
        bool can_setup = (s_caps.features & SL2_FEAT_WIFI_SETUP) != 0;
        bool healthy   = s->wifi_provisioned && s->wifi_connected;
        bool have_ip    = s_link.online && s->wifi_connected && s->wifi_ip[0];
        bool can_change = s_link.online && s->wifi_setup_avail && (!healthy || can_setup);
        const char *whint = NULL;
        if (have_ip) {
            whint = (healthy && can_change) ? H2(H_PRESS("show web UI"), H_HOLD("change network"))
                                            : H_PRESS("show web UI");
        } else if (can_change) {
            whint = healthy             ? H_HOLD("change network")
                  : s->wifi_provisioned ? H_PRESS("change network")
                                        : H_PRESS("set up");
        }
        if (whint) { ss_show(R_HINT); lv_label_set_text(SS.hint, whint); }
        else       { ss_hide(R_HINT); }
        lv_obj_add_flag(SS.wrow, LV_OBJ_FLAG_CLICKABLE);
    } else if (idx == SET_HOMEKIT) {
        const hvac_state_t *s = &s_state;
        bool paired = s->hk_paired > 0;
        const char *hktxt; uint32_t hkcol;
        if (!s_link.online) {
            hktxt = "Controller offline"; hkcol = COL_ALERT_NOLINK;
        } else if (paired) {
            hktxt = "Paired";        hkcol = COL_GREEN;
        } else {
            ss_show(R_HKCODE);
            lv_label_set_text(SS.hkcode, s->hk_code[0] ? s->hk_code : "---");
            hktxt = "Ready to pair"; hkcol = COL_GREY;
        }
        ss_show(R_HKROW);
        if (s_link.online && !paired) lv_label_set_text(SS.hint, H_PRESS("show QR"));
        else ss_hide(R_HINT);
        chip_status(SS.hkdot, SS.hkpaired, hkcol, hktxt);
        if (!s_link.online || !paired) lv_obj_add_flag(SS.hkrow, LV_OBJ_FLAG_CLICKABLE);
        else                             lv_obj_remove_flag(SS.hkrow, LV_OBJ_FLAG_CLICKABLE);
    } else if (idx == SET_LINK) {
        ss_show(R_LROW);
        uint32_t lc; const char *lt;
        if (s_link.online)   { lc = COL_GREEN;        lt = "Connected"; }
        else                 { lc = COL_ALERT_NOLINK; lt = "Offline";   }
        chip_status(SS.ldot, SS.lconn, lc, lt);
        if (s_zn_ui >= 1) {
            char nm[48]; zone_disp_name(s_zactive, nm, sizeof nm);
            lv_label_set_text(SS.lmeta, nm);
            ss_show(R_LMETA);
        }
        lv_label_set_text(SS.hint, s_link.bonded
            ? H2(H_PRESS("add zone"), H_HOLD("forget"))
            : H_PRESS("pair"));
        if (!s_link.online) lv_obj_add_flag(SS.lrow, LV_OBJ_FLAG_CLICKABLE);
        else                  lv_obj_remove_flag(SS.lrow, LV_OBJ_FLAG_CLICKABLE);
    } else if (idx == SET_SYSTEM) {
        const hvac_state_t *s = &s_state;
        ss_show(R_SYROW);
        if (s_caps.features & SL2_FEAT_LINK_SENSOR) {
            static const char *const SRC_NAME[] = { "Internal", "Sensor", "Link" };
            const char *nm = s->room_src_name[0] ? s->room_src_name :
                ((s->room_src <= SL2_ROOMSRC_LINK) ? SRC_NAME[s->room_src] : "Internal");
            uint32_t col = s->room_src_status == SL2_ROOMST_OK ? COL_GREEN : COL_ALERT_WARN;
            chip_status(SS.sydot, SS.syconn, col, nm);
            if (s->room_src_status == SL2_ROOMST_STALE)
                lv_label_set_text(SS.roomSrc, "Reading is stale");
            else if (s->room_src_status == SL2_ROOMST_UNAVAILABLE)
                lv_label_set_text(SS.roomSrc, "No reading available");
            else
                lv_label_set_text(SS.roomSrc, "Active temperature source");
            ss_show(R_ROOMSRC);
            /* With no source list a push opens an explanation, not the
             * picker, so don't promise one. */
            if (room_source_can_enter()) lv_label_set_text(SS.hint, H_PRESS("choose source"));
            else                         ss_hide(R_HINT);
        } else {
            chip_status(SS.sydot, SS.syconn, COL_GREY, "Internal");
            lv_label_set_text(SS.roomSrc, "Controller does not offer source selection");
            ss_show(R_ROOMSRC);
            ss_hide(R_HINT);
        }
        lv_obj_add_flag(SS.syrow, LV_OBJ_FLAG_CLICKABLE);
    } else if (idx == SET_ABOUT) {
        if (s_state.sensor_batt_pct >= 0) {
            lv_label_set_text_fmt(SS.abatt, "Sensor battery \xC2\xB7 %d%%", s_state.sensor_batt_pct);
            ss_show(R_ABATT);
        }
        ss_show(R_ABTN);
        ss_hide(R_HINT);
    } else if (idx == SET_ADVANCED) {
        ss_show(R_STATUS);
        lv_label_set_text(SS.status, "Open");
        lv_obj_set_style_text_color(SS.status, lv_color_hex(SET_COL[SET_ADVANCED]), 0);
        ss_hide(R_HINT);
    } else {
        ss_show(R_STATUS);
        /* Settings is usually opened from the Menu, but a dialog can jump
         * straight here from the home dial; name whichever one Back returns to. */
        lv_label_set_text(SS.status, ui_back_target() == SCREEN_MENU ? "Exit to menu"
                                                                    : "Exit to home");
        lv_obj_set_style_text_color(SS.status, lv_color_hex(COL_BACK), 0);
        ss_hide(R_HINT);
    }

    ss_rows_apply();
}

static void settings_open(int idx)
{
    switch (idx) {
        case SET_BACK:     ui_back();                         break;
        case SET_ADVANCED: ui_show_screen(SCREEN_ADVANCED);   break;
        default: break;
    }
}

void settings_press(void)
{
    if (s_set_sel == SET_BRIGHT) {
        s_set_editing = !s_set_editing;
        refresh_settings();
    } else if (s_set_sel == SET_ABOUT) {
        ui_show_screen(SCREEN_ABOUT);
    } else if (s_set_sel == SET_LINK) {
        if (s_pair_state == PAIR_UI_SEARCHING) {
            pair_cancel_request();
        } else if (s_pair_state != PAIR_UI_OK) {
            begin_pairing();
        }
    } else if (s_set_sel == SET_HOMEKIT) {
        if (s_link.online && s_state.hk_paired <= 0) homekit_qr_entry();
        else settings_chip_dialog(SET_HOMEKIT);
    } else if (s_set_sel == SET_WIFI) {
        wifi_setup_entry();
    } else if (s_set_sel == SET_SYSTEM) {
        if (room_source_can_enter()) ui_show_screen(SCREEN_ROOM_SOURCE);
        else                         settings_chip_dialog(s_set_sel);
    } else {
        s_set_editing = false;
        settings_open(s_set_sel);
    }
}

void settings_node_click_cb(lv_event_t *e)
{
    int idx = set_id((int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e)));
    s_set_sel = idx;
    if (idx == SET_BRIGHT) {
        s_set_editing = true;
        refresh_settings();
    } else if (idx == SET_WIFI || idx == SET_HOMEKIT || idx == SET_SYSTEM || idx == SET_ABOUT) {
        s_set_editing = false;
        refresh_settings();
    } else if (idx == SET_LINK) {
        s_set_editing = false;
        refresh_settings();
        if (!s_link.bonded) settings_press();
    } else {
        s_set_editing = false;
        settings_open(idx);
    }
}

lv_obj_t *settings_hold_arc(void) { return SS.holdArc; }

void settings_hold_arm(int set_row, uint32_t col)
{
    lv_obj_t *conn = (set_row == SET_WIFI) ? SS.wconn : SS.lconn;
    lv_obj_t *dot  = (set_row == SET_WIFI) ? SS.wdot  : SS.ldot;
    chip_status(dot, conn, col, H_HOLD("confirm"));
}

void settings_set_link_text(const char *t) { lv_label_set_text(SS.lconn, t); }

float settings_row_angle(int set_row) { return set_ang(set_row); }

static void set_chip_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx == SET_WIFI) { wifi_setup_entry(); return; }
    if (idx == SET_HOMEKIT && s_link.online && s_state.hk_paired <= 0) {
        homekit_qr_entry();
        return;
    }
    if (idx == SET_SYSTEM) {
        if (room_source_can_enter()) {
            ui_show_screen(SCREEN_ROOM_SOURCE);
            return;
        }
    }
    settings_chip_dialog(idx);
}

static void wifi_row_press_cb(lv_event_t *e)   { (void)e; s_set_sel = SET_WIFI; refresh_settings(); ui_button_down(); }
static void wifi_row_release_cb(lv_event_t *e) { (void)e; ui_button_up(); }
