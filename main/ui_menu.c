/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
#include "ui_rimlist.h"
#include "icons.h"
#include <string.h>
#include "sl2_proto.h"

static const hvac_mode_t MODEORDER[7] = { MODE_HEAT, MODE_COOL, MODE_HEAT_COOL,
                                          MODE_AUTO, MODE_DRY, MODE_FAN, MODE_OFF };

static struct { lv_obj_t *node[6], *dot[6], *cname, *cicon; } MP;

#define MENU_N 5
static const screen_id_t MENU_SCREEN[MENU_N] = {
    SCREEN_THERMOSTAT, SCREEN_MODE, SCREEN_FANVANE, SCREEN_ZONES, SCREEN_SETTINGS };

static const char *MENU_NAME[MENU_N]  = { "Temp", "Mode", "Airflow", "Zones", "Settings" };

static const uint32_t MENU_COL[MENU_N] = { COL_HEAT_GLOW, COL_ITEM_AMBER, COL_SYSTEM, COL_ITEM_SAGE, COL_LINK };

static const serin_icon_t *const MENU_ICON[MENU_N] = {
    &nav_temp, &nav_mode_mark, &mode_fan, &set_bridge, &nav_gear };

static rimlist_t s_menu;

static bool menu_keep(int m) {
    if (MENU_SCREEN[m] == SCREEN_ZONES   && s_zn_ui < 2) return false;
    if (MENU_SCREEN[m] == SCREEN_FANVANE && !airflow_available()) return false;
    return true;
}
static void menu_list_rebuild(void) { rimlist_build(&s_menu, MENU_N, menu_keep); }
static inline int menu_map(int i) { return rimlist_id(&s_menu, i); }

static int s_menu_sel = 0;

static int s_mode_sel = 0;

static rimlist_t s_mode;

static bool mode_supported(hvac_mode_t m);
static bool mode_keep(int i) { return mode_supported(MODEORDER[i]); }
static void modelist_rebuild(void) { rimlist_build(&s_mode, 7, mode_keep); }

static inline hvac_mode_t mode_val(int pos) { return MODEORDER[rimlist_id(&s_mode, pos)]; }

static int mode_order_index(hvac_mode_t m) {
    for (int i = 0; i < 7; i++) if (MODEORDER[i] == m) return i;
    return 0;
}
static int mode_index(hvac_mode_t m) { return rimlist_pos(&s_mode, mode_order_index(m)); }

static bool mode_supported(hvac_mode_t m) {
    switch (m) {
        case MODE_OFF:       return true;
        case MODE_HEAT:      return (s_caps.modes & (1u << SL2_MODE_HEAT)) != 0;
        case MODE_COOL:      return (s_caps.modes & (1u << SL2_MODE_COOL)) != 0;
        case MODE_HEAT_COOL: return (s_caps.modes & (1u << SL2_MODE_HEAT_COOL)) != 0;
        case MODE_AUTO:      return (s_caps.modes & (1u << SL2_MODE_AUTO)) != 0;
        case MODE_DRY:       return (s_caps.modes & (1u << SL2_MODE_DRY)) != 0;
        case MODE_FAN:       return (s_caps.modes & (1u << SL2_MODE_FAN_ONLY)) != 0;
        default:             return true;
    }
}

void menu_recheck(void)
{
    if (!s_panel[SCREEN_MENU]) return;
    int keep = menu_map((s_menu_sel >= 0 && s_menu_sel < s_menu.n) ? s_menu_sel : 0);
    if (!rimlist_moved(&s_menu, MENU_N, menu_keep)) return;
    s_menu_sel = rimlist_pos(&s_menu, keep);
    screen_rebuild(SCREEN_MENU);
}

void menu_caps_changed(void);

void mode_caps_changed(void);

void mode_enter(void)     { s_mode_sel = mode_index(s_state.mode); }

void menu_turn(int delta)
{
    s_menu_sel = rimlist_wrap(s_menu.n, s_menu_sel + delta);
    refresh_menu();
}

void mode_turn(int delta)
{
    s_mode_sel = rimlist_wrap(s_mode.n, s_mode_sel + delta);
    refresh_mode();
}

static void node_click_cb(lv_event_t *e);

static void mode_node_click_cb(lv_event_t *e);

static rim_t      MENR;
static lv_obj_t  *MEN_name;

void build_menu(void)
{
    lv_obj_t *p = s_panel[SCREEN_MENU] = mkpanel(COL_FACE, false);

    menu_list_rebuild();
    float ang[MENU_N]; uint32_t col[MENU_N]; const serin_icon_t *gly[MENU_N];
    for (int i = 0; i < s_menu.n; i++) {
        int m = menu_map(i);
        ang[i] = i * (360.0f / s_menu.n);
        col[i] = MENU_COL[m];
        gly[i] = MENU_ICON[m];
    }
    rim_cfg_t cfg = {
        .n = s_menu.n, .ang = ang, .col = col, .glyph = gly,
        .half_deg = 180.0f / s_menu.n - 8.0f,
        .glyph_r = SERIN_GLYPH_R,
        .disp_idle = 60, .disp_sel = 86,
        .node_px = (s_menu.n >= 5) ? 110 : 130,
        .on_tap = node_click_cb,
        .no_ind = true,
    };
    rim_build(&MENR, p, &cfg);

    lv_obj_t *c = mkcol(p, SERIN_PX(300), 8, 0);
    lv_obj_center(c);
    MEN_name = mklabel(c, FONT_SET_NAME, COL_HEAT_GLOW, "Temp");
    mklabel(c, FONT_HINT, COL_GREY, HINT_TURN_TAP);
}

void refresh_menu(void)
{
    rim_select(&MENR, s_menu_sel, MENU_COL[menu_map(s_menu_sel)]);
    lv_label_set_text(MEN_name, MENU_NAME[menu_map(s_menu_sel)]);
    lv_obj_set_style_text_color(MEN_name, lv_color_hex(MENU_COL[menu_map(s_menu_sel)]), 0);
}

static rim_t MR;

void build_mode(void)
{
    modelist_rebuild();
    lv_obj_t *p = s_panel[SCREEN_MODE] = mkpanel(COL_FACE, false);

    float ang[7]; uint32_t col[7]; const serin_icon_t *gly[7];
    for (int i = 0; i < s_mode.n; i++) {
        ang[i] = i * (360.0f / (float)s_mode.n);
        col[i] = mode_accent(mode_val(i));
        gly[i] = mode_icon(mode_val(i));
    }
    rim_cfg_t cfg = {
        .n = s_mode.n, .ang = ang, .col = col, .glyph = gly,
        .half_deg = s_mode.n > 6 ? 22.0f : 26.0f, .glyph_r = SERIN_GLYPH_R,
        .disp_idle = 54, .disp_sel = 80, .node_px = 110, .on_tap = mode_node_click_cb,
        .no_ind = true,
    };
    rim_build(&MR, p, &cfg);

    lv_obj_t *c = mkcol(p, SERIN_PX(240), 8, 0);
    lv_obj_center(c);
    MP.cname = mklabel(c, FONT_MODE_NAME, COL_HEAT_GLOW, "Heat");
    mklabel(c, FONT_HINT, COL_GREY, HINT_TURN_TAP);
}

void mode_destroy(void)
{
    MP.cname = NULL;
}

void refresh_mode(void)
{
    if (!s_panel[SCREEN_MODE]) return;
    uint32_t accent = mode_accent(mode_val(s_mode_sel));
    uint32_t b, g; const char *l; mode_pal(mode_val(s_mode_sel), &b, &g, &l);
    rim_select(&MR, s_mode_sel, accent);
    lv_label_set_text(MP.cname, l);
    lv_obj_set_style_text_color(MP.cname, lv_color_hex(accent), 0);
}

void mode_commit(void)
{
    s_state.mode = mode_val(s_mode_sel);
    s_state.status = (s_state.mode == MODE_OFF) ? ST_OFF : ST_ACTIVE;
    notify_edit();
    ui_show_screen(SCREEN_THERMOSTAT);
}

void menu_press(void)
{
    ui_show_screen(MENU_SCREEN[menu_map(s_menu_sel)]);
}

static void node_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_menu_sel = idx;
    ui_show_screen(MENU_SCREEN[menu_map(idx)]);
}

static void mode_node_click_cb(lv_event_t *e)
{
    s_mode_sel = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    mode_commit();
}

void menu_caps_changed(void)
{
    menu_recheck();
}

void mode_caps_changed(void)
{
    if (!s_panel[SCREEN_MODE]) return;
    if (!rimlist_moved(&s_mode, 7, mode_keep)) return;
    s_mode_sel = mode_index(s_state.mode);
    screen_rebuild(SCREEN_MODE);
}
