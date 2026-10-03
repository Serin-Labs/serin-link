/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#include "ui.h"
#include "serin_tokens.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_fonts_dial.h"
#include "child_lock.h"
#include "hk_proto.h"
#include "ui_setif.h"

typedef struct { bool bonded, online; uint8_t mac[6]; int rssi; } ui_link_t;

extern hvac_state_t s_state;
extern ui_caps_t    s_caps;
extern ui_link_t    s_link;

extern lv_obj_t    *s_panel[SCREEN_COUNT];
extern screen_id_t  s_cur;

enum { SET_BRIGHT, SET_WIFI, SET_LINK, SET_HOMEKIT, SET_SYSTEM, SET_ABOUT, SET_ADVANCED, SET_BACK, SET_N };
extern const uint32_t SET_COL[SET_N];
extern int s_set_sel;
void refresh_settings(void);
hk_proto_t hk_protocol(void);
void settings_land(int set_row);
void settings_chip_dialog(int idx);

bool room_source_can_enter(void);
void build_room_source(void);
void room_source_destroy(void);
void room_source_enter(void);
void refresh_room_source(void);
void room_source_turn(int delta);
void room_source_press(void);
void notify_room_source_id(uint64_t id);

void build_wifi(void);
void refresh_wifi(void);
void wifi_destroy(void);
void wifi_press(void);
void wifi_encoder(int delta);
void wifi_back(void);
void wifi_setup_entry(void);
void wifi_change_begin(void);

#define HINT_SEP        "\n"
#define H_PRESS(act)    "Press to " act
#define H_HOLD(act)     "Hold to " act
#define H2(a, b)        a HINT_SEP b

void note_activity(void);

void ui_hold_abort(void);

void screen_rebuild(screen_id_t id);

void notify_wifi_setup(void);
void notify_wifi_setup_session(uint32_t session);
void notify_wifi_cancel(uint32_t session);

#define OPA_FILL_NEUTRAL  26

#define REMIND_HERO_OPA  LV_OPA_30

extern ota_ui_t s_ota_st;
extern int      s_ota_pct;
extern char     s_ota_txt[32];
extern char     s_fw_ver[24];
extern char     s_fw_build[16];
bool ota_check_available(void);
void notify_ota_check(void);
void notify_ota_confirm(void);
void notify_ota_cancel(void);

void build_about(void);
void refresh_about(void);
void about_destroy(void);
void about_press(void);
void about_turn(int d);
void about_enter(void);
void about_caps_changed(void);
void build_update(void);
void refresh_update(void);
void ota_action(void);
void update_hold(void);
void update_turn(int delta);
void update_press(void);

void fault_code_str(char *out, size_t n, uint16_t code);

lv_obj_t *mkchip_btn(lv_obj_t *parent, const char *text, uint32_t txt_col,
                     uint32_t solid_bg  , lv_event_cb_t cb,
                     const touch_floor_arg_t *tf);

extern bool s_units_c;
extern int  s_brightness;
extern bool s_set_editing;

void ui_idle_off_set(bool off);
bool ui_idle_off_get(void);

void ui_night_en_set(bool on);
bool ui_night_en_get(void);
void ui_anim_en_set(bool on);
bool ui_anim_en_get(void);

extern pair_ui_t s_pair_state;
void pair_cancel_request(void);

extern ui_zone_info_t s_zinfo[UI_MAX_ZONES];
extern int s_zn_ui, s_zactive;

void build_settings(void);
void settings_press(void);
void settings_turn(int delta);
void settings_enter(void);
void settings_caps_changed(void);

lv_obj_t *settings_hold_arc(void);
void      settings_hold_arm(int set_row, uint32_t col);
void      settings_set_link_text(const char *t);
float     settings_row_angle(int set_row);

void apply_brightness(void);
void notify_edit(void);
void notify_units(bool use_f);
void begin_pairing(void);
void zone_disp_name(int i, char *out, size_t n);

void build_zones(void);
void refresh_zones(void);
void zones_commit(void);
void zones_turn(int delta);
bool zones_can_enter(void);
void zones_opened(void);
void zone_swipe(int step);
void zones_changed(int n);
void notify_zone_select(int i);
void notify_zones_open(void);

void anim_bg_opa_exec(void *obj, int32_t v);

void zchip_refresh(void);
void zchip_pulse(void);
/* Everything the UI knows about one HVAC mode, one row per hvac_mode_t: the
 * dial palette, the rim glyph, the mode's own name, the status word shown
 * while it is running and while it is not, and the SL2 capability bit that
 * offers it (0 = always offered).  Every site that used to switch on the mode
 * reads this instead. */
typedef struct {
    uint32_t            base;
    uint32_t            glow;
    const char         *name;
    const serin_icon_t *icon;
    const char         *running;
    const char         *idle;
    uint32_t            cap;
} mode_info_t;

/* Never NULL: a mode outside the enum falls back to the Off row, which is what
 * the switches' default arms did. */
const mode_info_t *mode_info(hvac_mode_t m);

uint32_t mode_accent(hvac_mode_t m);
void mode_pal(hvac_mode_t m, uint32_t *base, uint32_t *glow, const char **label);
const serin_icon_t *mode_icon(hvac_mode_t m);
const char *status_text(const hvac_state_t *s, bool active, bool off);

void build_fanvane(void);
void refresh_fanvane(void);
void fanvane_press(void);
void fan_turn(int delta);
void fanvane_enter(void);
void fanvane_caps_changed(void);
void fan_adopt(uint8_t pct);
bool airflow_available(void);

void build_menu(void);
void refresh_menu(void);
void menu_select_home(void);
void menu_press(void);
void menu_turn(int delta);
void menu_recheck(void);
void menu_caps_changed(void);
void build_mode(void);
void refresh_mode(void);
void mode_destroy(void);
void mode_commit(void);
void mode_turn(int delta);
void mode_enter(void);
void mode_caps_changed(void);

void build_advanced(void);
void refresh_advanced(void);
void advanced_destroy(void);
void advanced_turn(int delta);
void advanced_press(void);
lv_obj_t *advanced_hold_arc(void);
float advanced_row_angle(void);
bool advanced_on_reset_row(void);
void advanced_land_heatpump(void);

extern child_lock_t s_child_lock;
void child_lock_pill_flash(void);

typedef enum { DIAL_LIVE, DIAL_UNPAIRED, DIAL_LINK_OFF, DIAL_CN105_OFF,
               DIAL_WIFI_OFF } dial_face_t;

void home_hide_alert_chip(void);
void home_stop_alert_rim(void);
void face_dismiss(dial_face_t face);

bool home_pill_rect(lv_area_t *out);

bool    home_hero_rect(lv_area_t *out);
bool    home_airbtn_rect(lv_area_t *out);
int32_t home_arc_fill_deg(void);

bool face_dismissed(dial_face_t f);
void home_batt_dismiss_24h(void);
bool batt_snoozed(void);

bool home_face_live(void);

typedef enum { DLG_NONE, DLG_DISMISS_BATT, DLG_GOTO_HEATPUMP, DLG_GOTO_LINK,
               DLG_GOTO_LINK_ROW, DLG_PAIR, DLG_RESET } dlg_action_t;

void alert_dialog_show(uint32_t accent, const char *title, const char *body,
                       const char *primary, dlg_action_t action);
void info_dialog_show(uint32_t accent, const serin_icon_t *icon,
                      const char *title, const char *body);
void alert_dialog_hide(void);
void offline_dialog_show(dial_face_t face);
void batt_dialog_show(void);
void fault_dialog_show(const char *primary);
void reset_dialog_show(void);
void dlg_check_close(void);
/* Runs swap once the panel light has dipped to black, then relights.  False
 * when Transitions is off or the panel can't dip; make the change at once. */
bool ui_dip_swap(void (*swap)(void));
void dlg_close_by_user(int back);
void dlg_turn(int delta);
void dlg_press(void);
bool dlg_is_open(void);
bool dlg_resetting(void);

void coach_init(void);
bool coach_demo_turn(int delta);
bool coach_demo_tap(int dir);
bool coach_sandbox_active(void);
void coach_dismiss(void);
bool input_quiet(void);

lv_obj_t *mkpanel(uint32_t face, bool clickToMenu);
