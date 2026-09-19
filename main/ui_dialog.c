/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_arim.h"
#include "icons.h"
#include "factory_reset.h"
#include "sl2_proto.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *s_dlg = NULL;

static bool s_resetting = false;
bool dlg_resetting(void) { return s_resetting; }

static arim_t s_arim_reset;

#define DLG_BODY_W       300
#define DLG_HERO_PX       36
#define DLG_INFO_HERO_PX  45   /* lands on the icons' native 45 px bake on both panels */
#define DLG_COL_PAD_ROW  18
#define DLG_PILL_R       24
#define DLG_PILL_PAD_H   22
#define DLG_PILL_PAD_V   12
#define OPA_FILL_SEL     64

#define OFF_PAD_TOP_LINK  88
#define OFF_PAD_TOP_PUMP  62
#define OFF_BODY_W_LINK  338
#define OFF_BODY_W_PUMP  318
#define BATT_BODY_W      318
#define BATT_PAD_TOP      95
#define BATT_PILL_DROP    69
#define OFF_DOT           10
#define OFF_RAIL_W       328
#define OFF_NODE_DOT      20
#define OFF_NODE_H        28

static void off_searching_row(lv_obj_t *col)
{
    lv_obj_t *row = mkrow(col, SERIN_PX(9));
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dot = mkdot(row, SERIN_PX(OFF_DOT), COL_ALERT_NOLINK);

    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, anim_bg_opa_exec);
    lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_reverse_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    mklabel(row, FONT_HINT, COL_SEARCH, "Searching for controller");
}

static lv_obj_t    *s_dlg_btn[2];
static int          s_dlg_sel = 0;
static dlg_action_t s_dlg_action;
static uint8_t      s_dlg_sig;
static int          s_dlg_nbtn = 2;
static bool         s_dlg_has_action = true;

/* Which live signature, if any, closes the dialog out from under the user.
 * STATIC dialogs describe nothing live, so nothing should tear them down. */
enum { DLG_SCOPE_ALERT, DLG_SCOPE_SET, DLG_SCOPE_STATIC };
static uint8_t      s_dlg_scope;
static dial_face_t  s_dlg_dismiss_face = DIAL_LIVE;

static bool         s_dlg_press_dismiss    = false;
static bool         s_dlg_backdrop_dismiss = false;
static lv_obj_t    *s_dlg_ring             = NULL;

#define DLG_SEL_DISMISS  1
#define DLG_PILL_GAP      8
#define OFF_PILL_DROP_LINK 30
#define OFF_PILL_DROP_PUMP  0
static void offline_dialog_build(bool link_off, const char *primary,
                                 dlg_action_t action);
static void reset_face_show(void);

static uint8_t dlg_alert_sig(void)
{
    if (!s_link.bonded)                 return 0;
    if (s_state.alert != ALERT_NONE)    return (uint8_t)(1 + s_state.alert);
    if (!s_state.cn105_connected)       return 10;
    if (s_state.sensor_batt_low
        && !batt_snoozed()) return 11;
    return 12;
}

static uint8_t dlg_set_sig(void)
{
    return (uint8_t)((s_link.bonded           ? 1u  : 0u)
                   | (s_link.online           ? 2u  : 0u)
                   | (s_state.wifi_connected  ? 4u  : 0u)
                   | (s_state.hk_paired > 0   ? 8u  : 0u)
                   | (s_state.err_count       ? 16u : 0u)
                   | (s_state.cn105_connected ? 32u : 0u));
}

void dlg_check_close(void)
{
    if (!s_dlg) return;
    if (s_dlg_scope == DLG_SCOPE_STATIC) return;
    uint8_t now = (s_dlg_scope == DLG_SCOPE_SET) ? dlg_set_sig() : dlg_alert_sig();
    if (now != s_dlg_sig) alert_dialog_hide();
}

/* With Transitions on, dialogs the user opens or closes swap while the panel
 * light is dipped.  A closing dialog stays up as the leaver until then; an
 * opening one (and the reset face) waits hidden. */
static lv_obj_t *s_dlg_leaver;
static lv_obj_t *s_reset_face;

static void dlg_dark(void)
{
    if (s_dlg_leaver) { lv_obj_delete(s_dlg_leaver); s_dlg_leaver = NULL; }
    if (s_dlg)        lv_obj_remove_flag(s_dlg, LV_OBJ_FLAG_HIDDEN);
    if (s_reset_face) lv_obj_remove_flag(s_reset_face, LV_OBJ_FLAG_HIDDEN);
}

static void dlg_hide(bool by_user)
{
    if (!s_dlg) return;

    arim_stop(s_dlg_ring);
    s_dlg_ring = NULL;

    bool shown = !lv_obj_has_flag(s_dlg, LV_OBJ_FLAG_HIDDEN);
    if (by_user && shown && !s_dlg_leaver && ui_dip_swap(dlg_dark))
        s_dlg_leaver = s_dlg;
    else
        lv_obj_delete(s_dlg);
    s_dlg = NULL;
    s_dlg_dismiss_face = DIAL_LIVE;
}

void alert_dialog_hide(void) { dlg_hide(false); }

static void dlg_apply_sel(void)
{
    if (s_dlg_action == DLG_RESET) {
        bool r = (s_dlg_sel == 0);
        lv_obj_set_style_bg_color(s_dlg_btn[0], lv_color_hex(COL_ALERT_NOLINK), 0);
        lv_obj_set_style_bg_opa(s_dlg_btn[0], r ? LV_OPA_COVER : OPA_FILL_NEUTRAL, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_dlg_btn[0], 0),
                                    lv_color_hex(r ? COL_ON_LIGHT : COL_ALERT_NOLINK), 0);
        lv_obj_set_style_bg_opa(s_dlg_btn[1], r ? OPA_FILL_NEUTRAL : OPA_FILL_SEL, 0);
        return;
    }
    for (int i = 0; i < s_dlg_nbtn; i++)
        lv_obj_set_style_bg_opa(s_dlg_btn[i],
            i == s_dlg_sel ? OPA_FILL_SEL : OPA_FILL_NEUTRAL, 0);
}

static void dlg_do_action(void)
{
    dlg_action_t act = s_dlg_action;
    dlg_hide(true);
    if (act != DLG_DISMISS_BATT && child_lock_engaged(&s_child_lock) && home_face_live()) { child_lock_pill_flash(); return; }
    switch (act) {
        case DLG_NONE:
            break;
        case DLG_DISMISS_BATT:
            home_batt_dismiss_24h();
            home_hide_alert_chip();
            zchip_refresh();
            break;
        case DLG_GOTO_HEATPUMP:
            advanced_land_heatpump();
            break;
        case DLG_GOTO_LINK:
            settings_land(SET_LINK);
            break;
        case DLG_GOTO_LINK_ROW:
            s_set_sel = SET_LINK; refresh_settings();
            break;
        case DLG_PAIR:
            begin_pairing();
            break;
        case DLG_RESET:
            reset_face_show();
            break;
    }
}

void dlg_close_by_user(int sel)
{
    if (s_dlg_dismiss_face != DIAL_LIVE) face_dismiss(s_dlg_dismiss_face);
    if (sel == 0 && s_dlg_has_action) dlg_do_action();
    else                              dlg_hide(true);
}

static void dlg_btn_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    dlg_close_by_user(i);
}

static void dlg_backdrop_click_cb(lv_event_t *e)
{
    if (!s_dlg_backdrop_dismiss) return;
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
    dlg_close_by_user(DLG_SEL_DISMISS);
}

static lv_obj_t *dlg_frame(dlg_action_t action, bool has_action,
                           bool top_anchor, int pad_top)
{
    if (s_pair_state != PAIR_UI_HIDDEN) return NULL;
    alert_dialog_hide();
    coach_dismiss();

    s_dlg_action           = action;
    s_dlg_sel              = 0;
    s_dlg_scope            = (s_cur == SCREEN_SETTINGS) ? DLG_SCOPE_SET : DLG_SCOPE_ALERT;
    s_dlg_sig              = (s_dlg_scope == DLG_SCOPE_SET) ? dlg_set_sig() : dlg_alert_sig();
    s_dlg_has_action       = has_action;
    s_dlg_press_dismiss    = false;
    s_dlg_backdrop_dismiss = false;

    lv_obj_t *ov = s_dlg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, SERIN_DISP, SERIN_DISP);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(COL_FACE), 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ov, dlg_backdrop_click_cb, LV_EVENT_CLICKED, NULL);
    if (ui_dip_swap(dlg_dark)) lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);

    home_stop_alert_rim();

    lv_obj_t *col = mkcol(ov, LV_SIZE_CONTENT, DLG_COL_PAD_ROW, TF_DLG_COL_PAD);
    if (top_anchor) {
        lv_obj_align(col, LV_ALIGN_TOP_MID, 0, SERIN_PX(pad_top));
    } else {
        lv_obj_align(col, LV_ALIGN_CENTER, 0, TF_DLG_COL_PAD / 2);
    }
    return col;
}

static lv_obj_t *dlg_pill_row(lv_obj_t *col)
{
    return mkpillrow(col, TF_GAP_DLG_ROW, SERIN_PX(DLG_PILL_GAP));
}

static lv_obj_t *dlg_pill(lv_obj_t *row, const char *text, uint32_t txt_col,
                          const touch_floor_arg_t *tf, int idx)
{
    lv_obj_t *btn = s_dlg_btn[idx] =
        mkpill(row, text, txt_col, DLG_PILL_R, DLG_PILL_PAD_H, DLG_PILL_PAD_V);
    lv_obj_set_style_bg_color(btn, lv_color_white(), 0);
    lv_obj_set_user_data(btn, (void *)(intptr_t)idx);
    lv_obj_add_event_cb(btn, dlg_btn_click_cb, LV_EVENT_CLICKED, NULL);
    press_ack(btn, ACK_OPA_CHIP);
    touch_floor(btn, tf);
    return btn;
}

static void alert_dialog_build(uint32_t accent, const serin_icon_t *icon, int hero_px,
                               const char *title, const char *body,
                               const char *primary, dlg_action_t action)
{
    lv_obj_t *col = dlg_frame(action, primary != NULL, false, 0);
    if (!col) return;
    s_dlg_nbtn = s_dlg_has_action ? 2 : 1;

    mkicon(col, icon, SERIN_PX(hero_px), accent);
    mklabel(col, FONT_SET_NAME, accent, title);
    mkbody(col, FONT_STATUS_SM, COL_DIM, body, SERIN_PX(DLG_BODY_W));

    lv_obj_t *row = dlg_pill_row(col);

    const char *lbl[2] = { s_dlg_has_action ? primary : "Close", "Close" };
    static const touch_floor_arg_t k_dlgbtn_tf[2] = { { TF_CAP_DLG_ROW, "s_dlg_btn[0]" },
                                                      { TF_CAP_DLG_ROW, "s_dlg_btn[1]" } };
    static const touch_floor_arg_t k_dlgbtn_solo_tf = { 0, "s_dlg_btn[0] (solo)" };
    for (int i = 0; i < s_dlg_nbtn; i++)
        dlg_pill(row, lbl[i], (i == 0 && s_dlg_has_action) ? accent : COL_WHITE,
                 s_dlg_nbtn > 1 ? &k_dlgbtn_tf[i] : &k_dlgbtn_solo_tf, i);

    dlg_apply_sel();
}

void alert_dialog_show(uint32_t accent, const char *title, const char *body,
                       const char *primary, dlg_action_t action)
{
    alert_dialog_build(accent, &hero_alert, DLG_HERO_PX, title, body, primary, action);
}

/* Explains a setting rather than reporting a condition: leads with the row's own
 * glyph, carries no action, and outlives any state change while it is up. */
void info_dialog_show(uint32_t accent, const serin_icon_t *icon,
                      const char *title, const char *body)
{
    alert_dialog_build(accent, icon, DLG_INFO_HERO_PX, title, body, NULL, DLG_NONE);
    if (!s_dlg) return;
    s_dlg_scope            = DLG_SCOPE_STATIC;
    s_dlg_press_dismiss    = true;
    s_dlg_backdrop_dismiss = true;
}

void settings_chip_dialog(int idx)
{
    static const char NOLINK_BODY[] =
        "This status comes from the Serin controller, which the dial "
        "can't reach right now. Restore the link to update it.";
    switch (idx) {
        case SET_WIFI:
            if (!s_link.online)
                alert_dialog_show(COL_ALERT_NOLINK, "No link", NOLINK_BODY,
                                  "Go to Link", DLG_GOTO_LINK_ROW);
            else if (!s_state.wifi_connected)
                alert_dialog_show(COL_GREY, "Wi-Fi offline",
                    "The Serin controller is not connected to a Wi-Fi "
                    "network. Smart home control and firmware updates are "
                    "unavailable until it reconnects.",
                    NULL, DLG_GOTO_LINK_ROW);
            break;
        case SET_HOMEKIT:
            if (!s_link.online)
                alert_dialog_show(COL_ALERT_NOLINK, "No link", NOLINK_BODY,
                                  "Go to Link", DLG_GOTO_LINK_ROW);
            else if (s_state.hk_paired <= 0)
                alert_dialog_show(COL_GREY, "Ready to pair",
                    hk_protocol() == HK_PROTO_MATTER
                        ? "The controller is not paired with a home yet. In "
                          "your home app (Apple Home, Google Home, or "
                          "Alexa), choose Add Accessory and scan or enter "
                          "the setup code."
                        : "The controller is not added to Apple Home yet. "
                          "In the Home app, choose Add Accessory and enter "
                          "the setup code shown on this screen.",
                    NULL, DLG_GOTO_LINK_ROW);
            break;
        case SET_LINK:
            if (s_link.online) break;
            if (!s_link.bonded)
                alert_dialog_show(COL_ALERT_NOLINK, "Link offline",
                    "The dial is not paired with a Serin controller, so "
                    "there is nothing to control yet.",
                    "Pair", DLG_PAIR);
            else
                offline_dialog_build(true, NULL, DLG_GOTO_LINK_ROW);
            break;
        /* Only reached with no source list to open, so every branch explains
         * why there is nothing to choose -- never a heat pump fault, which
         * has its own row under Advanced. */
        case SET_SYSTEM:
            if (!s_link.online)
                alert_dialog_show(COL_ALERT_NOLINK, "No link", NOLINK_BODY,
                                  "Go to Link", DLG_GOTO_LINK_ROW);
            else if (!(s_caps.features & SL2_FEAT_LINK_SENSOR))
                alert_dialog_show(COL_GREY, "Room Sensor",
                    "The heat pump's own sensor measures the room. This "
                    "controller offers no other source to choose from.",
                    NULL, DLG_GOTO_LINK_ROW);
            else if (s_state.room_src_status == SL2_ROOMST_STALE)
                alert_dialog_show(COL_ALERT_WARN, "Reading is stale",
                    "The temperature source has not reported recently. The "
                    "heat pump keeps running on its own sensor until it "
                    "does.",
                    NULL, DLG_GOTO_LINK_ROW);
            else if (s_state.room_src_status == SL2_ROOMST_UNAVAILABLE)
                alert_dialog_show(COL_ALERT_WARN, "No reading available",
                    "The temperature source has no reading. Check its "
                    "power and its range from the controller.",
                    NULL, DLG_GOTO_LINK_ROW);
            else
                alert_dialog_show(COL_GREY, "Room Sensor",
                    "The controller lists no sensors to choose from. Add one "
                    "in its web interface, then pick it here.",
                    NULL, DLG_GOTO_LINK_ROW);
            break;
        default: break;
    }
}

static void off_chain_rail(lv_obj_t *col)
{
    const bool  ok[3]   = { true, s_link.online, s_state.cn105_connected };
    const char *name[3] = { "This dial", "Controller", "Heat pump" };
    const char *stat[3] = { "OK",
                            s_link.online           ? "Linked" : "No reply",
                            s_state.cn105_connected ? "Linked" : "No reply" };

    lv_obj_t *rail = lv_obj_create(col);
    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, SERIN_PX(OFF_RAIL_W), SERIN_PX(3 * OFF_NODE_H));
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rail, 0, 0);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    for (int seg = 0; seg < 2; seg++) {
        lv_obj_t *ln = lv_obj_create(rail);
        lv_obj_remove_style_all(ln);
        lv_obj_add_flag(ln, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(ln, SERIN_PX(2), SERIN_PX(OFF_NODE_H));
        lv_obj_set_pos(ln, SERIN_PX(OFF_NODE_DOT / 2 - 1),
                           SERIN_PX(OFF_NODE_H / 2 + seg * OFF_NODE_H));
        lv_obj_set_style_bg_color(ln,
            lv_color_hex(ok[seg + 1] ? COL_GREEN : COL_ALERT_WARN), 0);
        lv_obj_set_style_bg_opa(ln, LV_OPA_COVER, 0);
        lv_obj_remove_flag(ln, LV_OBJ_FLAG_CLICKABLE);
    }

    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(rail);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), SERIN_PX(OFF_NODE_H));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, SERIN_PX(15), 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *dot = mkdot(row, SERIN_PX(OFF_NODE_DOT), COL_GREEN);
        if (!ok[i]) {
            lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(dot, SERIN_PX(3), 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(COL_ALERT_WARN), 0);
            lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
        }
        lv_obj_set_style_outline_width(dot, SERIN_PX(4), 0);
        lv_obj_set_style_outline_color(dot, lv_color_hex(COL_FACE), 0);
        lv_obj_set_style_outline_opa(dot, LV_OPA_COVER, 0);

        lv_obj_t *nm = mklabel(row, FONT_STATUS, COL_WHITE, name[i]);
        lv_obj_set_flex_grow(nm, 1);
        mklabel(row, FONT_HINT, ok[i] ? COL_GREEN : COL_ALERT_WARN, stat[i]);
    }
}

static void dlg_alert_ring(uint32_t accent, bool sweep)
{
    s_arim_dlg = (arim_t){ .col = accent, .opa = LV_OPA_COVER };
    s_dlg_ring = arim_ring_create(s_dlg, &s_arim_dlg);
    lv_obj_move_to_index(s_dlg_ring, 0);
    if (sweep) arim_sweep_start(s_dlg_ring);
    else       arim_breath_start(s_dlg_ring);
}

static void dlg_explainer_chrome(uint32_t accent, bool sweep)
{
    s_dlg_nbtn             = 1;
    s_dlg_press_dismiss    = true;
    s_dlg_backdrop_dismiss = true;

    dlg_alert_ring(accent, sweep);
}

static void dlg_primary_chip(lv_obj_t *row, const char *txt, uint32_t accent,
                             const touch_floor_arg_t *tf)
{
    lv_obj_t *btn = s_dlg_btn[0] =
        mkchip_btn(row, txt, COL_ON_LIGHT, accent, dlg_btn_click_cb, tf);
    lv_obj_set_user_data(btn, (void *)(intptr_t)0);
}

static void offline_dialog_build(bool link_off, const char *primary, dlg_action_t action)
{
    uint32_t accent = link_off ? COL_ALERT_NOLINK : COL_ALERT_WARN;

    lv_obj_t *col = dlg_frame(action, primary != NULL, true,
                              link_off ? OFF_PAD_TOP_LINK : OFF_PAD_TOP_PUMP);
    if (!col) return;

    dlg_explainer_chrome(accent, link_off);

    mkicon(col, &hero_alert, SERIN_PX(36), accent);
    mklabel(col, link_off ? FONT_SET_NAME : FONT_DIAL_PILL, accent,
            link_off ? "Link offline" : "Heat pump offline");

    if (!link_off) off_chain_rail(col);

    lv_obj_t *b = mklabel(col, FONT_DIAL_NOW, COL_DIM,
        link_off ? "Can't reach the controller. Check that it has power."
                 : "Issue is on the CN105 cable. Check the connections.");
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(b, SERIN_PX(link_off ? OFF_BODY_W_LINK : OFF_BODY_W_PUMP));
    lv_obj_set_style_text_align(b, LV_TEXT_ALIGN_CENTER, 0);

    if (link_off) off_searching_row(col);

    static const touch_floor_arg_t k_offpill_tf = { 0, "s_dlg_btn[0] (offline)" };
    lv_obj_t *row = dlg_pill_row(col);
    lv_obj_set_style_pad_top(row,
        SERIN_PX(link_off ? OFF_PILL_DROP_LINK : OFF_PILL_DROP_PUMP) + TF_ROW_PAD, 0);

    if (primary) {
        dlg_primary_chip(row, primary, accent, &k_offpill_tf);

        lv_obj_t *drow = mkpillrow(col, TF_GAP_DLG_ROW, 0);
        static const touch_floor_arg_t k_offdis_tf = { 0, "s_dlg_btn[1] (offline dismiss)" };
        lv_obj_t *dis = dlg_pill(drow, "Dismiss", COL_WHITE, &k_offdis_tf, DLG_SEL_DISMISS);
        lv_obj_set_style_bg_opa(dis, OPA_FILL_NEUTRAL, 0);
    } else {
        dlg_pill(row, "Close", COL_WHITE, &k_offpill_tf, 0);
        dlg_apply_sel();
    }
}

void offline_dialog_show(dial_face_t face)
{
    bool link_off = (face == DIAL_LINK_OFF);
    bool takeover = !face_dismissed(face);

    offline_dialog_build(link_off, "Settings",
                         link_off ? DLG_GOTO_LINK : DLG_GOTO_HEATPUMP);

    if (dlg_is_open())         s_dlg_dismiss_face = takeover ? face : DIAL_LIVE;
    else if (takeover) face_dismiss(face);
}

void batt_dialog_show(void)
{
    lv_obj_t *col = dlg_frame(DLG_DISMISS_BATT, true, true, BATT_PAD_TOP);
    if (!col) return;

    dlg_explainer_chrome(COL_ALERT_WARN, false);

    mkicon(col, &hero_batt, SERIN_PX(104), COL_ALERT_WARN);

    mklabel(col, FONT_DIAL_PILL, COL_ALERT_WARN, "Sensor battery low");

    mkbody(col, FONT_DIAL_NOW, COL_DIM,
        "The remote temperature sensor's battery is running low. "
        "Replace it soon to keep room readings accurate.",
        SERIN_PX(BATT_BODY_W));

    static const touch_floor_arg_t k_battpill_tf = { 0, "s_dlg_btn[0] (batt)" };
    lv_obj_t *row = dlg_pill_row(col);
    lv_obj_set_style_pad_top(row, SERIN_PX(BATT_PILL_DROP) + TF_ROW_PAD, 0);
    dlg_primary_chip(row, "Dismiss 24h", COL_ALERT_WARN, &k_battpill_tf);
}

void fault_dialog_show(const char *primary)
{
    char body[224];
    if (s_state.error_code) {
        char code[8];
        fault_code_str(code, sizeof code, s_state.error_code);
        snprintf(body, sizeof body,
                 "The heat pump has stopped and reported fault code %s. The dial "
                 "can't clear it, so write the code down for whoever services it. "
                 "This clears on its own if the heat pump recovers.", code);
    } else {
        snprintf(body, sizeof body,
                 "The heat pump has stopped and reported a fault, but not which one. "
                 "The dial can't clear it. This clears on its own if the heat pump "
                 "recovers.");
    }
    alert_dialog_show(COL_ALERT_WARN, "Heat pump fault", body, primary, DLG_GOTO_HEATPUMP);
}

void reset_dialog_show(void)
{
    lv_obj_t *col = dlg_frame(DLG_RESET, true, false, 0);
    if (!col) return;
    s_dlg_nbtn = 2;
    dlg_alert_ring(COL_ALERT_NOLINK, false);

    mkicon(col, &hero_alert, SERIN_PX(36), COL_ALERT_NOLINK);
    mklabel(col, FONT_SET_NAME, COL_ALERT_NOLINK, "Reset link?");
    mkbody(col, FONT_STATUS_SM, COL_DIM,
        "Erases all pairings, Wi-Fi setup choices, and preferences. "
        "Your heat pump keeps running.", SERIN_PX(DLG_BODY_W));

    lv_obj_t *row = dlg_pill_row(col);
    static const touch_floor_arg_t k_reset_tf[2] = {
        { 0, "s_dlg_btn[0] (reset)"  },
        { 0, "s_dlg_btn[1] (cancel)" },
    };
    dlg_pill(row, "Reset", COL_ALERT_NOLINK, &k_reset_tf[0], 0);
    lv_obj_t *drow = mkpillrow(col, TF_GAP_DLG_ROW, 0);
    dlg_pill(drow, "Cancel", COL_WHITE, &k_reset_tf[1], 1);
    dlg_apply_sel();
}

static void reset_wipe_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    factory_reset_execute();
}

static void reset_face_show(void)
{
    s_resetting = true;

    lv_obj_t *ov = s_reset_face = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, SERIN_DISP, SERIN_DISP);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(COL_FACE), 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    if (ui_dip_swap(dlg_dark)) lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);

    s_arim_reset = (arim_t){ .col = COL_ALERT_NOLINK, .opa = LV_OPA_COVER };
    lv_obj_t *ring = arim_ring_create(ov, &s_arim_reset);
    lv_obj_move_to_index(ring, 0);
    arim_breath_start(ring);

    lv_obj_t *c = mkcol(ov, LV_SIZE_CONTENT, DLG_COL_PAD_ROW, TF_DLG_COL_PAD);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, TF_DLG_COL_PAD / 2);

    mkicon(c, &set_reset, SERIN_PX(60), COL_ALERT_NOLINK);
    mklabel(c, FONT_SET_NAME, COL_ALERT_NOLINK, "Resetting");
    mklabel(c, FONT_STATUS_SM, COL_DIM, "Takes a moment");

    lv_timer_create(reset_wipe_cb, 800, NULL);
}

bool dlg_is_open(void) { return s_dlg != NULL; }

const char *ui_dlg_state(void)
{
    if (!dlg_is_open()) return "none";
    return (s_dlg_action == DLG_RESET) ? "reset" : "other";
}

void dlg_turn(int delta)
{
    s_dlg_sel = ((s_dlg_sel + delta) % s_dlg_nbtn + s_dlg_nbtn) % s_dlg_nbtn;
    dlg_apply_sel();
}

void dlg_press(void)
{
    dlg_close_by_user(s_dlg_press_dismiss ? DLG_SEL_DISMISS : s_dlg_sel);
}
