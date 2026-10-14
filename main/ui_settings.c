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
#include "ui_gesture.h"
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
static void settings_add_zone_click_cb(lv_event_t *e);
static void settings_nav_click_cb(lv_event_t *e);
static void wifi_row_press_cb(lv_event_t *e);
static void wifi_row_release_cb(lv_event_t *e);

static int s_set_landing = -1;
/* Classifies Wi-Fi row touches so any long press can eat its release tap. */
static uig_t *s_wrow_gest;

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
static struct { lv_obj_t *name, *brHit, *brTrack, *brFill, *brPct,
                         *navbtn, *status, *hint,
                         *wrow, *wdot, *wconn,
                         *hkcode, *hkrow, *hkpaired, *hkdot,
                         *center, *lrow, *lconn, *lmeta, *lactions, *ladd, *holdArc,
                         *syrow, *sydot, *syconn, *roomSrc,
                         *abatt, *abtn; } SS;

/* Fixed centers in the 480 px design canvas. Hidden details never reflow the
 * title or controls. The 230 px control width also clears the seven-stop ring. */
enum { SET_TITLE_Y = SERIN_SLOT_TITLE_Y, SET_DETAIL_Y = SERIN_SLOT_DETAIL_Y,
       SET_CONTROL_Y = SERIN_SLOT_CONTROL_Y, SET_HINT_TOP = SERIN_SLOT_HINT_TOP,
       SET_CONTROL_W = SERIN_SLOT_CONTROL_W };

static void settings_place(lv_obj_t *obj, int y)
{
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, SERIN_PX(y) - SERIN_C);
}

static lv_obj_t *settings_detail(lv_obj_t *parent, const lv_font_t *font,
                                  uint32_t color, const char *text)
{
    lv_obj_t *label = mklabel(parent, font, color, text);
    lv_obj_set_size(label, SERIN_PX(260), lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    settings_place(label, SET_DETAIL_Y);
    return label;
}

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
    if (dot) lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
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

static void settings_action_press_cb(lv_event_t *e)
{
    lv_obj_t *pill = lv_event_get_user_data(e);
    lv_obj_set_state(pill, LV_STATE_PRESSED, lv_event_get_code(e) == LV_EVENT_PRESSED);
}

static lv_obj_t *settings_hit(lv_obj_t *parent, int width)
{
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    /* The brightness track is below its value; reserve that visual space even
     * on the 480 px panel, whose physical touch floor is smaller. */
    int height = SERIN_PX(SERIN_SLOT_CONTROL_H);
    if (height < SERIN_TOUCH_MIN) height = SERIN_TOUCH_MIN;
    lv_obj_set_size(hit, SERIN_PX(width), height);
    lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    return hit;
}

/* Reserve the full touch height without enlarging the visible pill or allowing
 * invisible horizontal growth to cover a neighboring ring control. */
static lv_obj_t *settings_action_slot(lv_obj_t *parent, int width, lv_obj_t **pill)
{
    lv_obj_t *hit = settings_hit(parent, width);

    *pill = mkrow(hit, SERIN_PX(10));
    settings_chip(*pill);
    lv_obj_set_size(*pill, LV_SIZE_CONTENT,
                    lv_font_get_line_height(FONT_DIAL_PILL) + 2 * SERIN_PX(11));
    lv_obj_set_style_max_width(*pill, SERIN_PX(width), 0);
    lv_obj_set_style_pad_hor(*pill, SERIN_PX(16), 0);
    lv_obj_set_style_pad_ver(*pill, 0, 0);
    lv_obj_remove_flag(*pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(*pill);
    press_ack(*pill, ACK_OPA_CHIP);
    lv_obj_add_event_cb(hit, settings_action_press_cb, LV_EVENT_PRESSED, *pill);
    lv_obj_add_event_cb(hit, settings_action_press_cb, LV_EVENT_RELEASED, *pill);
    lv_obj_add_event_cb(hit, settings_action_press_cb, LV_EVENT_PRESS_LOST, *pill);
    return hit;
}

static lv_obj_t *settings_status_row(lv_obj_t *parent, int set_id, const char *placeholder,
                                     lv_obj_t **out_dot, lv_obj_t **out_label)
{
    lv_obj_t *pill;
    lv_obj_t *hit = settings_action_slot(parent, SET_CONTROL_W, &pill);
    lv_obj_set_user_data(hit, (void *)(intptr_t)set_id);
    lv_obj_add_event_cb(hit, set_chip_click_cb, LV_EVENT_CLICKED, NULL);
    *out_dot = mkdot(pill, SERIN_PX(14), COL_GREEN);
    *out_label = mklabel(pill, FONT_DIAL_PILL, COL_GREEN, placeholder);
    lv_obj_set_style_max_width(*out_label, SERIN_PX(SET_CONTROL_W - 32 - 14 - 10), 0);
    lv_obj_set_height(*out_label, lv_font_get_line_height(FONT_DIAL_PILL));
    lv_label_set_long_mode(*out_label, LV_LABEL_LONG_SCROLL);
    settings_place(hit, SET_CONTROL_Y);
    return hit;
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

    lv_obj_t *c = SS.center = lv_obj_create(p);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    SS.name = mklabel(c, FONT_SET_NAME, COL_ITEM_AMBER, "Brightness");
    settings_place(SS.name, SET_TITLE_Y);

    SS.brHit = settings_hit(c, SET_CONTROL_W);
    settings_place(SS.brHit, SET_CONTROL_Y);
    lv_obj_add_event_cb(SS.brHit, bright_track_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(SS.brHit, bright_track_cb, LV_EVENT_PRESSING, NULL);
    SS.brTrack = lv_obj_create(SS.brHit);
    lv_obj_remove_style_all(SS.brTrack);
    lv_obj_set_size(SS.brTrack, SERIN_PX(188), SERIN_PX(10));
    lv_obj_set_style_radius(SS.brTrack, SERIN_PX(5), 0);
    lv_obj_set_style_bg_opa(SS.brTrack, (lv_opa_t)36, 0);
    lv_obj_set_style_bg_color(SS.brTrack, lv_color_hex(COL_PURE), 0);
    lv_obj_remove_flag(SS.brTrack, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(SS.brTrack, LV_ALIGN_CENTER, 0, SERIN_PX(32));
    SS.brFill = lv_obj_create(SS.brTrack);
    lv_obj_remove_style_all(SS.brFill);
    lv_obj_set_size(SS.brFill, SERIN_PX(188), SERIN_PX(10));
    lv_obj_set_style_radius(SS.brFill, SERIN_PX(5), 0);
    lv_obj_set_style_bg_opa(SS.brFill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(SS.brFill, lv_color_hex(SET_COL[SET_BRIGHT]), 0);
    lv_obj_align(SS.brFill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_remove_flag(SS.brFill, LV_OBJ_FLAG_CLICKABLE);

    SS.brPct = mklabel(SS.brHit, FONT_BRIGHT, COL_WHITE, "100%");
    lv_obj_align(SS.brPct, LV_ALIGN_CENTER, 0, -SERIN_PX(5));

    lv_obj_t *nav_pill;
    SS.navbtn = settings_action_slot(c, SET_CONTROL_W, &nav_pill);
    SS.status = mklabel(nav_pill, FONT_DIAL_PILL, COL_WHITE, "Open");
    lv_obj_add_event_cb(SS.navbtn, settings_nav_click_cb, LV_EVENT_CLICKED, NULL);
    settings_place(SS.navbtn, SET_CONTROL_Y);

    SS.wrow = settings_status_row(c, SET_WIFI, "Connected", &SS.wdot, &SS.wconn);
    lv_obj_add_event_cb(SS.wrow, wifi_row_press_cb,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(SS.wrow, wifi_row_press_cb,   LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(SS.wrow, wifi_row_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(SS.wrow, wifi_row_release_cb, LV_EVENT_PRESS_LOST, NULL);
    static const uig_cfg_t k_wrow_gest = { 0 };
    s_wrow_gest = uig_attach(SS.wrow, &k_wrow_gest);

    SS.hkcode = settings_detail(c, FONT_DIAL_NOW, COL_WHITE, "548-94-669");

    SS.hkrow = settings_status_row(c, SET_HOMEKIT, "Paired",
                                   &SS.hkdot, &SS.hkpaired);

    SS.lmeta = settings_detail(c, FONT_STATUS, COL_DIM, "");

    SS.lactions = mkrow(c, SERIN_PX(6));
    lv_obj_remove_flag(SS.lactions, LV_OBJ_FLAG_SCROLLABLE);
    settings_place(SS.lactions, SET_CONTROL_Y);
    lv_obj_t *status_pill, *add_pill;
    SS.lrow = settings_action_slot(SS.lactions, 112, &status_pill);
    lv_obj_set_width(status_pill, lv_pct(100));
    lv_obj_set_style_pad_hor(status_pill, SERIN_PX(4), 0);
    lv_obj_set_user_data(SS.lrow, (void *)(intptr_t)SET_LINK);
    lv_obj_add_event_cb(SS.lrow, set_chip_click_cb, LV_EVENT_CLICKED, NULL);
    SS.lconn = mklabel(status_pill, FONT_STATUS, COL_GREEN, "Connected");
    /* Hold feedback wraps without moving Add zone over a ring control. */
    lv_obj_set_width(SS.lconn, 0);
    lv_obj_set_flex_grow(SS.lconn, 1);
    lv_label_set_long_mode(SS.lconn, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(SS.lconn, LV_TEXT_ALIGN_CENTER, 0);
    SS.ladd = settings_action_slot(SS.lactions, 112, &add_pill);
    lv_obj_set_width(add_pill, lv_pct(100));
    lv_obj_set_style_pad_hor(add_pill, SERIN_PX(4), 0);
    mklabel(add_pill, FONT_STATUS, COL_WHITE, "Add zone");
    lv_obj_add_event_cb(SS.ladd, settings_add_zone_click_cb, LV_EVENT_CLICKED, NULL);

    SS.syrow = settings_status_row(c, SET_SYSTEM, "Connected",
                                   &SS.sydot, &SS.syconn);

    SS.roomSrc = settings_detail(c, FONT_STATUS, COL_DIM, "");

    SS.abatt = settings_detail(c, FONT_STATUS, COL_DIM, "");

    lv_obj_t *about_pill;
    SS.abtn = settings_action_slot(c, SET_CONTROL_W, &about_pill);
    mklabel(about_pill, FONT_DIAL_PILL, COL_WHITE, "Details");
    lv_obj_add_event_cb(SS.abtn, about_details_cb, LV_EVENT_CLICKED, NULL);
    settings_place(SS.abtn, SET_CONTROL_Y);

    SS.hint   = mklabel(c, FONT_HINT, COL_GREY, "");
    lv_obj_set_width(SS.hint, SERIN_PX(280));
    lv_obj_set_style_text_align(SS.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(SS.hint, LV_ALIGN_TOP_MID, 0, SERIN_PX(SET_HINT_TOP));

    SS.holdArc = rim_arc(p, SET_LINK_ANG, SERIN_IND_HALF_DEG,
                         SERIN_PX(SERIN_RIM_W_SEL), COL_ALERT_NOLINK);
    lv_obj_add_flag(SS.holdArc, LV_OBJ_FLAG_HIDDEN);
}

enum { R_BRIGHT, R_STATUS, R_WROW, R_HKCODE, R_HKROW, R_LACTIONS, R_LMETA,
       R_SYROW, R_ROOMSRC, R_ABATT, R_ABTN, R_HINT, R_LADD, R_N };
_Static_assert(R_N <= 16, "Settings visibility mask is too small");
static uint16_t s_ss_want;
static void ss_rows_begin(void) { s_ss_want = 1u << R_HINT; }
static void ss_show(int r) { s_ss_want |=  (uint16_t)(1u << r); }
static void ss_hide(int r) { s_ss_want &= (uint16_t)~(1u << r); }
static void ss_rows_apply(void)
{
    lv_obj_t *const rows[R_N] = { SS.brHit, SS.navbtn, SS.wrow, SS.hkcode,
                                  SS.hkrow, SS.lactions, SS.lmeta, SS.syrow,
                                  SS.roomSrc, SS.abatt, SS.abtn, SS.hint, SS.ladd };
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
        ss_show(R_BRIGHT);
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
        ss_show(R_LACTIONS);
        uint32_t lc; const char *lt;
        if (s_link.online)   { lc = COL_GREEN;        lt = "Connected"; }
        else                 { lc = COL_ALERT_NOLINK; lt = "Offline";   }
        chip_status(NULL, SS.lconn, lc, lt);
        if (s_zn_ui >= 1) {
            char nm[48]; zone_disp_name(s_zactive, nm, sizeof nm);
            lv_label_set_text(SS.lmeta, nm);
            ss_show(R_LMETA);
        }
        if (s_link.bonded) ss_show(R_LADD);
        lv_label_set_text(SS.hint, s_link.bonded ? H_HOLD("forget") : H_PRESS("pair"));
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
        ss_hide(R_HINT);
    } else {
        ss_show(R_STATUS);
        lv_label_set_text(SS.status, "Return");
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

static void settings_nav_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_open(s_set_sel);
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

static void settings_add_zone_click_cb(lv_event_t *e)
{
    if (s_cur == SCREEN_SETTINGS && s_set_sel == SET_LINK && s_link.bonded &&
        lv_event_get_current_target(e) == SS.ladd)
        settings_press();
}

lv_obj_t *settings_hold_arc(void) { return SS.holdArc; }

void settings_hold_arm(int set_row, uint32_t col)
{
    lv_obj_t *conn = (set_row == SET_WIFI) ? SS.wconn : SS.lconn;
    lv_obj_t *dot  = (set_row == SET_WIFI) ? SS.wdot  : NULL;
    chip_status(dot, conn, col, H_HOLD("confirm"));
}

void settings_set_link_text(const char *t) { lv_label_set_text(SS.lconn, t); }

void settings_eat_wifi_click(void) { uig_eat_click(s_wrow_gest); }

float settings_row_angle(int set_row) { return set_ang(set_row); }

static void set_chip_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx == SET_WIFI) {
        if (!s_wrow_gest || uig_click_is_tap(s_wrow_gest)) wifi_setup_entry();
        return;
    }
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

static void wifi_row_press_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_LONG_PRESSED) {
        uig_eat_click(s_wrow_gest);
        return;
    }
    s_set_sel = SET_WIFI;
    refresh_settings();
    ui_button_down();
}
static void wifi_row_release_cb(lv_event_t *e) { (void)e; ui_button_up(); }
