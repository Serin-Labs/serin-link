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

static struct { lv_obj_t *cname, *description, *action, *band_cool; } MP;

#define MENU_N 6
#define MENU_HOME 0
#define MENU_BACK (MENU_N - 1)
static const screen_id_t MENU_SCREEN[MENU_N] = {
    SCREEN_THERMOSTAT, SCREEN_MODE, SCREEN_FANVANE, SCREEN_ZONES, SCREEN_SETTINGS,
    SCREEN_THERMOSTAT };

static const char *MENU_NAME[MENU_N]  = { "Home", "Mode", "Airflow", "Zones", "Settings", "Back" };

static const uint32_t MENU_COL[MENU_N] = { COL_HEAT_GLOW, COL_ITEM_AMBER, COL_SYSTEM, COL_ITEM_SAGE, COL_LINK, COL_BACK };

static const serin_icon_t *const MENU_ICON[MENU_N] = {
    &nav_temp, &nav_mode_mark, &mode_fan, &set_link_row, &nav_gear, &nav_back };

static rimlist_t s_menu;

static bool menu_keep(int m) {
    if (MENU_SCREEN[m] == SCREEN_ZONES   && s_zn_ui < 2) return false;
    if (MENU_SCREEN[m] == SCREEN_FANVANE && !airflow_available()) return false;
    return true;
}
static void menu_list_rebuild(void) { rimlist_build(&s_menu, MENU_N, menu_keep); }
static inline int menu_map(int i) { return rimlist_id(&s_menu, i); }

static int s_menu_sel = 0;

void menu_select_home(void)
{
    s_menu_sel = rimlist_pos(&s_menu, MENU_HOME);
}

static int s_mode_sel = 0;

static rimlist_t s_mode;

static bool mode_supported(hvac_mode_t m);
static bool mode_keep(int i) { return mode_supported(MODEORDER[i]); }
static void modelist_rebuild(void) { rimlist_build(&s_mode, 7, mode_keep); }

/* The rim carries one stop per supported mode plus a trailing Back, which
 * rim_stop_angle pins to RIM_BACK_ANG the way every other rim screen does. */
static inline int  mode_stops(void)       { return s_mode.n + 1; }
static inline bool mode_is_back(int pos)  { return pos >= s_mode.n; }

static inline hvac_mode_t mode_val(int pos) { return MODEORDER[rimlist_id(&s_mode, pos)]; }

static int mode_order_index(hvac_mode_t m) {
    for (int i = 0; i < 7; i++) if (MODEORDER[i] == m) return i;
    return 0;
}
static int mode_index(hvac_mode_t m) { return rimlist_pos(&s_mode, mode_order_index(m)); }

static bool mode_supported(hvac_mode_t m) {
    uint32_t cap = mode_info(m)->cap;
    return !cap || (s_caps.modes & cap) != 0;
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
    s_mode_sel = rimlist_wrap(mode_stops(), s_mode_sel + delta);
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
    int back_pos = rimlist_pos(&s_menu, MENU_BACK);
    float ang[MENU_N]; uint32_t col[MENU_N]; const serin_icon_t *gly[MENU_N];
    for (int i = 0; i < s_menu.n; i++) {
        int m = menu_map(i);
        ang[i] = rim_stop_angle(i, s_menu.n, back_pos);
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
}

void refresh_menu(void)
{
    rim_select(&MENR, s_menu_sel, MENU_COL[menu_map(s_menu_sel)]);
    lv_label_set_text(MEN_name, MENU_NAME[menu_map(s_menu_sel)]);
    lv_obj_set_style_text_color(MEN_name, lv_color_hex(MENU_COL[menu_map(s_menu_sel)]), 0);
}

static rim_t MR;

static void mode_action_click_cb(lv_event_t *e)
{
    (void)e;
    mode_commit();
}

void build_mode(void)
{
    modelist_rebuild();
    lv_obj_t *p = s_panel[SCREEN_MODE] = mkpanel(COL_FACE, false);

    int nn = mode_stops();
    float ang[8]; uint32_t col[8]; const serin_icon_t *gly[8];
    for (int i = 0; i < s_mode.n; i++) {
        ang[i] = rim_stop_angle(i, nn, s_mode.n);
        col[i] = mode_accent(mode_val(i));
        gly[i] = mode_icon(mode_val(i));
    }
    ang[s_mode.n] = rim_stop_angle(s_mode.n, nn, s_mode.n);
    col[s_mode.n] = COL_BACK;
    gly[s_mode.n] = &nav_back;
    rim_cfg_t cfg = {
        .n = nn, .ang = ang, .col = col, .glyph = gly,
        /* Match the selector ring radius so the center action has a full,
         * unobstructed target even with all seven modes available. */
        .half_deg = 180.0f / (float)nn - 4.0f, .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 54, .disp_sel = 80, .node_px = 100, .on_tap = mode_node_click_cb,
        .no_ind = true,
    };
    rim_build(&MR, p, &cfg);

    /* A paired heat/cool mark distinguishes range control from Auto without
     * adding another bitmap family. Both marks fit inside one rim target. */
    for (int i = 0; i < s_mode.n; i++) {
        if (mode_val(i) != MODE_HEAT_COOL) continue;
        icon_set(MR.glyph[i], &mode_heat, COL_HEAT_GLOW);
        MP.band_cool = mkicon(MR.node[i], &mode_cool, SERIN_PX(32), COL_COOL_GLOW);
    }

    MP.cname = mkslot_label(p, FONT_MODE_NAME, COL_HEAT_GLOW, "Heat", 260, SERIN_SLOT_TITLE_Y);
    MP.description = mkslot_label(p, FONT_HINT, COL_DIM, "", 240, SERIN_SLOT_DETAIL_Y);
    mkslot_action(p, "Use mode", mode_action_click_cb, &MP.action);
}

void mode_destroy(void)
{
    memset(&MP, 0, sizeof MP);
}

void refresh_mode(void)
{
    if (!s_panel[SCREEN_MODE]) return;
    if (s_mode_sel >= mode_stops()) s_mode_sel = mode_stops() - 1;
    if (s_mode_sel < 0)             s_mode_sel = 0;
    if (mode_is_back(s_mode_sel)) {
        rim_select(&MR, s_mode_sel, COL_BACK);
        if (MP.band_cool) {
            int band = mode_index(MODE_HEAT_COOL);
            icon_fit(MR.glyph[band], &mode_heat, SERIN_PX(32));
            icon_fit(MP.band_cool, &mode_cool, SERIN_PX(32));
            lv_obj_set_style_opa(MP.band_cool, LV_OPA_40, 0);
        }
        lv_label_set_text(MP.cname, "Back");
        lv_obj_set_style_text_color(MP.cname, lv_color_hex(COL_BACK), 0);
        lv_obj_add_flag(MP.description, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(MP.action, "Return");
        return;
    }
    uint32_t accent = mode_accent(mode_val(s_mode_sel));
    uint32_t b, g; const char *l; mode_pal(mode_val(s_mode_sel), &b, &g, &l);
    rim_select(&MR, s_mode_sel, accent);
    if (MP.band_cool) {
        int band = mode_index(MODE_HEAT_COOL);
        bool selected = band == s_mode_sel;
        int size = SERIN_PX(selected ? 42 : 32);
        icon_fit(MR.glyph[band], &mode_heat, size);
        icon_fit(MP.band_cool, &mode_cool, size);
        lv_obj_set_style_opa(MP.band_cool, selected ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    lv_label_set_text(MP.cname, l);
    lv_label_set_text(MP.action, "Use mode");
    lv_obj_set_style_text_color(MP.cname, lv_color_hex(accent), 0);
    hvac_mode_t mode = mode_val(s_mode_sel);
    const char *description = mode == MODE_HEAT_COOL ? "Sets a temperature range"
                            : mode == MODE_AUTO ? "Automatic mode" : "";
    lv_label_set_text(MP.description, description);
    if (description[0]) lv_obj_remove_flag(MP.description, LV_OBJ_FLAG_HIDDEN);
    else                lv_obj_add_flag(MP.description, LV_OBJ_FLAG_HIDDEN);
}

void mode_commit(void)
{
    if (mode_is_back(s_mode_sel)) { ui_back(); return; }
    if (sync_refuse()) return;
    s_state.mode = mode_val(s_mode_sel);
    s_state.status = (s_state.mode == MODE_OFF) ? ST_OFF : ST_ACTIVE;
    notify_edit();
    ui_show_screen(SCREEN_THERMOSTAT);
}

/* The Back stop retraces the trail; every other stop opens its screen, and
 * "Home" is the deliberate jump to the root rather than a step back. */
static void menu_open(int pos)
{
    int m = menu_map(pos);
    if (m == MENU_BACK) { ui_back(); return; }
    /* Every Airflow adjustment is a change, so refuse it at the door. */
    if (MENU_SCREEN[m] == SCREEN_FANVANE && sync_refuse()) return;
    ui_show_screen(MENU_SCREEN[m]);
}

void menu_press(void)
{
    menu_open(s_menu_sel);
}

static void node_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_menu_sel = idx;
    menu_open(idx);
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
