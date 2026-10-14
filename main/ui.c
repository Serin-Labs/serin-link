/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui.h"
#include "ui_theme.h"
#include "ui_widgets.h"
#include "ui_rim.h"
#include "ui_arim.h"
#include "ui_internal.h"
#include "icons.h"
#include "serin_tokens.h"
#include "serin_input.h"
#include "ui_gesture.h"
#include "ui_fonts_dial.h"
#include "src/core/lv_obj_event_private.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "espnow_client.h"
#include "sl2_proto.h"
#include "ui_dd.h"
#include "branding.h"
#include "prefs.h"
#include "sensor_sht40.h"
#include "thermal_cal.h"
#include "link_ota.h"

hvac_state_t s_state;
screen_id_t  s_cur = SCREEN_THERMOSTAT;
int          s_set_sel = 0;
int          s_brightness = 100;
static bool         s_dimmed = false;
static int  s_bright_cur = 100;
static lv_timer_t  *s_idle_timer;
static bool         s_glancing = false;
static int          s_glance_dx, s_glance_dy;
static bool         s_idle_off = false;
static bool         s_disp_asleep = false;
static bool         s_refresh_pending = false;
static ui_screen_gate_t s_screen_gate = UI_SCREEN_GATE_NONE;
static bool s_night      = false;
static uint8_t s_night_ceil = 0;
static bool s_night_en   = true;
static bool s_anim_en    = true;
static bool s_night_lift = false;
#if !CONFIG_SERIN_BOARD_VIEWE_21
static void bright_exec_cb(void *var, int32_t v);
#endif
child_lock_t        s_child_lock;
bool         s_units_c = false;
ui_caps_t s_caps = {
    .modes        = 0x0077,
    .features     = 0x00FF,
    .fan_steps    = 5,
    .fan_has_auto = true,
    .vane_v       = SL2_VANECAP(5, true, true),
    .vane_h       = SL2_VANECAP(6, false, true) | SL2_VANECAP_SPLIT_TOP,
    .set_min_dc   = 160,
    .set_max_dc   = 305,
    .set_step_dc  = 5,
    .ftab_id      = 1,
    .band_min_gap_dc = 5,
};
static inline int   caps_step_dd(const ui_caps_t *c, bool units_c) {
    return units_c ? (c->set_step_dc ? c->set_step_dc : 10) : 10;
}
static inline int   caps_ddmin(const ui_caps_t *c, bool units_c) {
    if (!units_c) return sl2_dc_to_display(c->set_min_dc, true, c->ftab_id) * 10;
    int st = caps_step_dd(c, true);
    return ((c->set_min_dc + st - 1) / st) * st;
}
static inline int   caps_ddmax(const ui_caps_t *c, bool units_c) {
    if (!units_c) return sl2_dc_to_display(c->set_max_dc, true, c->ftab_id) * 10;
    int st = caps_step_dd(c, true);
    return (c->set_max_dc / st) * st;
}
static inline int   dial_step_dd(void) { return caps_step_dd(&s_caps, s_units_c); }
static inline int   dial_ddmin(void)   { return caps_ddmin(&s_caps, s_units_c); }
static inline int   dial_ddmax(void)   { return caps_ddmax(&s_caps, s_units_c); }
static bool         s_edit = false;
static lv_timer_t  *s_edit_timer;
static bool         s_drag_engaged;
static bool         s_drag_anchored;
static float        s_drag_ang0;
static float        s_drag_ang;
static float        s_drag_accum;
static uint32_t     s_drag_last_tx;
static uig_t       *s_dial_gest;
static lv_timer_t  *s_hold_timer;
static bool         s_btn_down;
static lv_obj_t *s_root;
lv_obj_t    *s_panel[SCREEN_COUNT];

static struct { lv_obj_t *arc, *roomDot, *setDot, *pill, *status, *num, *numHalf, *deg,
                         *now, *alert, *alertIcon, *alertTxt, *nolinkRing,
                         *pairIcon, *pairBox, *pairTitle, *pairInstr, *pairHint,
                         *pairRow, *pairBtn, *pairBtnLbl, *pairBtn2,
                         *offIcon, *wifiHint, *holdArc, *lockPill,
                         *zchip, *zname, *airbtn,
                         *bandLo, *bandLoHalf, *bandHi, *bandHiHalf,
                         *bandSep, *bandRing, *bandTapL, *bandTapR, *setDot2; } T;
ui_link_t s_link;
ui_zone_info_t s_zinfo[UI_MAX_ZONES];
int  s_zn_ui, s_zactive;
static void (*s_on_zone_select)(int);
static void (*s_on_zones_open)(void);
pair_ui_t   s_pair_state = PAIR_UI_HIDDEN;
static lv_timer_t *s_pair_cd;
static int         s_pair_left;
static uint8_t     s_pair_ch;
static void ui_state_defaults(hvac_state_t *s);

ota_ui_t s_ota_st = OTA_UI_HIDDEN;
int      s_ota_pct;
char     s_ota_txt[48];
char     s_fw_ver[24];
char     s_fw_build[16];

#if CONFIG_SERIN_THERMAL_CAL
static bool s_cal_owned;
static bool s_cal_writing;
#endif

void ui_set_link_state(bool bonded, bool online, const uint8_t mac[6], int rssi)
{
#if CONFIG_SERIN_THERMAL_CAL
    if (s_cal_owned && !s_cal_writing) return;
#endif
    bool same = s_link.bonded == bonded && s_link.online == online &&
                s_link.rssi == rssi && (!mac || memcmp(s_link.mac, mac, 6) == 0);
    if (same) return;
    s_link.bonded = bonded;
    s_link.online = online;
    if (mac) memcpy(s_link.mac, mac, 6);
    s_link.rssi = rssi;
    dlg_check_close();
    if (s_cur == SCREEN_SETTINGS && !s_hold_timer) refresh_settings();
}

hk_proto_t hk_protocol(void)
{
    return hk_proto_from(s_state.hk_payload, s_state.hk_code);
}

static void (*s_on_edit)(void);
void ui_set_on_edit(void (*cb)(void)) { s_on_edit = cb; }
void notify_edit(void) { if (s_on_edit) s_on_edit(); }

static void (*s_on_bright)(int);
void ui_set_on_brightness(void (*cb)(int)) { s_on_bright = cb; }

static void (*s_on_disp_sleep)(bool);
void ui_set_on_display_sleep(void (*cb)(bool)) { s_on_disp_sleep = cb; }

static void (*s_on_units)(bool);
void ui_set_on_units(void (*cb)(bool)) { s_on_units = cb; }
void notify_units(bool use_f) { if (s_on_units) s_on_units(use_f); }

static void (*s_on_room_source_id)(uint64_t);
void ui_set_on_room_source_id(void (*cb)(uint64_t)) { s_on_room_source_id = cb; }
void notify_room_source_id(uint64_t id) {
    if (s_on_room_source_id) s_on_room_source_id(id);
}

static void (*s_on_pair)(void);
void ui_set_on_pair(void (*cb)(void)) { s_on_pair = cb; }

static void (*s_on_pair_cancel)(void);
void ui_set_on_pair_cancel(void (*cb)(void)) { s_on_pair_cancel = cb; }
void pair_cancel_request(void) { if (s_on_pair_cancel) s_on_pair_cancel(); }

static void (*s_on_forget)(void);
void ui_set_on_forget(void (*cb)(void)) { s_on_forget = cb; }

static void (*s_on_wifi_setup)(void);
void ui_set_on_wifi_setup(void (*cb)(void)) { s_on_wifi_setup = cb; }
void notify_wifi_setup(void) { if (s_on_wifi_setup) s_on_wifi_setup(); }
static void (*s_on_wifi_setup_session)(uint32_t);
static void (*s_on_wifi_cancel)(uint32_t);
void ui_set_on_wifi_setup_session(void (*cb)(uint32_t)) { s_on_wifi_setup_session = cb; }
void ui_set_on_wifi_cancel(void (*cb)(uint32_t)) { s_on_wifi_cancel = cb; }
void notify_wifi_setup_session(uint32_t session) {
    if (s_on_wifi_setup_session) s_on_wifi_setup_session(session);
}
void notify_wifi_cancel(uint32_t session) {
    if (s_on_wifi_cancel) s_on_wifi_cancel(session);
}

void apply_brightness(void)
{
    s_night_lift = true;
#if !CONFIG_SERIN_BOARD_VIEWE_21
    lv_anim_delete(&s_bright_cur, bright_exec_cb);
#endif
    s_bright_cur = s_brightness;
    if (s_on_bright) s_on_bright(s_brightness);
}

static void (*s_on_ota_check)(void);
static void (*s_on_ota_confirm)(void);
static void (*s_on_ota_cancel)(void);
void ui_set_on_ota_check(void (*cb)(void))   { s_on_ota_check = cb; }
void ui_set_on_ota_confirm(void (*cb)(void)) { s_on_ota_confirm = cb; }
void ui_set_on_ota_cancel(void (*cb)(void))  { s_on_ota_cancel = cb; }
bool ota_check_available(void) { return s_on_ota_check != NULL; }
void notify_ota_check(void)    { if (s_on_ota_check)   s_on_ota_check();   }
void notify_ota_confirm(void)  { if (s_on_ota_confirm) s_on_ota_confirm(); }
void notify_ota_cancel(void)   { if (s_on_ota_cancel)  s_on_ota_cancel();  }
void ui_set_fw_version(const char *v) { snprintf(s_fw_ver, sizeof(s_fw_ver), "%s", v ? v : ""); }
void ui_set_fw_build(const char *d) { snprintf(s_fw_build, sizeof(s_fw_build), "%s", d ? d : ""); }

#define ACK_OPA_PILL   90
#define ACK_OPA_CHIP   64
#define ACK_OPA_TILE   20
#define ACK_OPA_UNITS  38

#define IDLE_DIM_MS   30000
#define IDLE_DIM_PCT  15
#define IDLE_POLL_MS  250

#define NIGHT_CEIL_PCT 20

#define RAMP_UP_MS    600
#define RAMP_DOWN_MS 1000

#define IDLE_GLANCE_MS   120000
#define GLANCE_HERO_OPA  LV_OPA_60
#define GLANCE_SHIFT_PX  4

#define HERO_DEG_OPA     LV_OPA_50

#define NOW_TXT_OPA      LV_OPA_60

#if !CONFIG_SERIN_BOARD_VIEWE_21
static int wake_target(void)
{
    int ceil = s_night_ceil ? s_night_ceil : NIGHT_CEIL_PCT;
    if (ui_night_active() && !s_night_lift && s_brightness > ceil)
        return ceil;
    return s_brightness;
}

static int rest_target(void)
{
    int wake = wake_target();
    return (wake < IDLE_DIM_PCT) ? wake : IDLE_DIM_PCT;
}

static void bright_exec_cb(void *var, int32_t v)
{
    (void)var;
    s_bright_cur = (int)v;
    if (s_on_bright) s_on_bright((int)v);
}

static void bright_ramp_to(int target, uint32_t ms)
{
    lv_anim_delete(&s_bright_cur, bright_exec_cb);
    if (target == s_bright_cur) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_bright_cur);
    lv_anim_set_values(&a, s_bright_cur, target);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, bright_exec_cb);
    lv_anim_start(&a);
}
#endif

static void idle_set_dim(bool dim)
{
    if (dim == s_dimmed) return;
    s_dimmed = dim;
    if (!s_on_bright) return;
#if CONFIG_SERIN_BOARD_VIEWE_21
    s_on_bright(s_brightness);
#else
    if (dim) {
        s_night_lift = false;
        bright_ramp_to(rest_target(), RAMP_DOWN_MS);
    } else {
        bright_ramp_to(wake_target(), RAMP_UP_MS);
    }
#endif
}

void note_activity(void)
{
    lv_display_trigger_activity(NULL);
    thermal_cal_note_input();
    idle_set_dim(false);
}

#define LOCK_PILL_MS  1200
static lv_timer_t *s_lock_pill_timer;
static void refresh_thermostat(void);
static dial_face_t thermo_face(void);
static bool face_is_blank(dial_face_t f);

static void lock_pill_hide_cb(lv_timer_t *t)
{
    (void)t;
    if (T.lockPill) lv_obj_add_flag(T.lockPill, LV_OBJ_FLAG_HIDDEN);
    s_lock_pill_timer = NULL;
    if (s_cur == SCREEN_THERMOSTAT) refresh_thermostat();
}

void child_lock_pill_flash(void)
{
    if (!T.lockPill) return;
    /* A blank offline face centres its label below the lock pill's slot. */
    if (!face_is_blank(thermo_face())) lv_obj_add_flag(T.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(T.lockPill);
    lv_obj_remove_flag(T.lockPill, LV_OBJ_FLAG_HIDDEN);
    if (s_lock_pill_timer) lv_timer_reset(s_lock_pill_timer);
    else s_lock_pill_timer = lv_timer_create(lock_pill_hide_cb, LOCK_PILL_MS, NULL);
    lv_timer_set_repeat_count(s_lock_pill_timer, 1);
}

static bool lock_pill_owns_slot(void)
{
    return T.lockPill && !lv_obj_has_flag(T.lockPill, LV_OBJ_FLAG_HIDDEN);
}

static inline bool dim_visible(void)
{
#if CONFIG_SERIN_BOARD_VIEWE_21
    return false;
#else
    return s_dimmed;
#endif
}

static void refresh_thermostat(void);
static void refresh_cur_screen(void);

/* Brightness dip.  On an emissive panel the light itself can fade: dim to
 * black, make the change in the dark, and relight once the new frame has
 * reached the panel.  Only the light changes during the ramps, so they cost
 * no redraws.  The ramp runs on its own short timer because LVGL's animation
 * timer only ticks at the refresh period.  Everything queued with dip_swap
 * runs at the same dark point, so a dialog closing into a new screen, or a
 * screen change followed by sleep, shares one dip. */
#define DIP_TICK_MS   8
#define DIP_SWAPS     4
#define SLEEP_FADE_MS 300
static void (*s_on_bright_scale)(uint8_t);
void ui_set_on_bright_scale(void (*cb)(uint8_t)) { s_on_bright_scale = cb; }

static uint32_t s_dip_out_ms = UI_DIP_OUT_MS, s_dip_in_ms = UI_DIP_IN_MS;
void ui_dip_ms_set(uint32_t out_ms, uint32_t in_ms) { s_dip_out_ms = out_ms; s_dip_in_ms = in_ms; }
void ui_dip_ms_get(uint32_t *out_ms, uint32_t *in_ms) { *out_ms = s_dip_out_ms; *in_ms = s_dip_in_ms; }

static lv_timer_t *s_dip_timer;
static uint32_t    s_dip_t0, s_dip_ms, s_dip_relight_ms;
static uint8_t     s_dip_from, s_dip_to, s_dip_cur = 255;
static void      (*s_dip_done)(void);
static void      (*s_dip_relight_done)(void);
static bool        s_dip_run, s_dip_relight;
static void      (*s_dip_swap[DIP_SWAPS])(void);
static lv_obj_t   *s_splash;

static void dip_tick(lv_timer_t *t)
{
    (void)t;
    uint32_t el = lv_tick_elaps(s_dip_t0);
    if (el >= s_dip_ms) {
        s_dip_cur = s_dip_to;
        s_on_bright_scale(s_dip_cur);
        lv_timer_pause(s_dip_timer);
        s_dip_run = false;
        void (*done)(void) = s_dip_done;
        s_dip_done = NULL;
        if (done) done();
        return;
    }
    s_dip_cur = (uint8_t)(s_dip_from + ((int)s_dip_to - s_dip_from) * (int)el / (int)s_dip_ms);
    s_on_bright_scale(s_dip_cur);
}

static void dip_start(uint8_t to, uint32_t ms, void (*done)(void))
{
    s_dip_relight = false;
    if (!s_dip_timer) s_dip_timer = lv_timer_create(dip_tick, DIP_TICK_MS, NULL);
    s_dip_from = s_dip_cur;
    s_dip_to   = to;
    s_dip_ms   = ms ? ms : 1;
    s_dip_done = done;
    s_dip_run  = true;
    s_dip_t0   = lv_tick_get();
    lv_timer_resume(s_dip_timer);
    lv_timer_reset(s_dip_timer);
}

static void dip_refr_ready_cb(lv_event_t *e)
{
    (void)e;
    if (!s_dip_relight) return;
    dip_start(255, s_dip_relight_ms, s_dip_relight_done);
}

static void dip_relight_after_frame(uint32_t ms, void (*done)(void))
{
    static bool hooked;
    if (!hooked) {
        lv_display_add_event_cb(lv_display_get_default(), dip_refr_ready_cb,
                                LV_EVENT_REFR_READY, NULL);
        hooked = true;
    }
    s_dip_relight_ms   = ms;
    s_dip_relight_done = done;
    s_dip_relight      = true;
}

static void dip_dark(void)
{
    void (*run[DIP_SWAPS])(void);
    memcpy(run, s_dip_swap, sizeof run);
    memset(s_dip_swap, 0, sizeof s_dip_swap);
    for (int i = 0; i < DIP_SWAPS; i++)
        if (run[i]) run[i]();
    if (s_dip_run) return;   /* a swap queued another dip */
    if (s_disp_asleep) {
        /* The panel is off; restore the scale so wake starts from it. */
        s_dip_cur = 255;
        s_on_bright_scale(255);
    } else {
        dip_relight_after_frame(s_dip_in_ms, NULL);
    }
}

/* Queues swap to run once the light is at 0.  False when there is no light
 * to dip (sim, 2.1), the splash owns the light, or the queue is full; the
 * caller then makes its change at once. */
static bool dip_swap(void (*swap)(void), uint32_t out_ms)
{
    if (!s_on_bright_scale || s_splash) return false;
    int i = 0;
    while (i < DIP_SWAPS && s_dip_swap[i] && s_dip_swap[i] != swap) i++;
    if (i == DIP_SWAPS) return false;
    s_dip_swap[i] = swap;
    if (!(s_dip_run && s_dip_to == 0)) dip_start(0, out_ms, dip_dark);
    return true;
}

bool ui_dip_swap(void (*swap)(void)) { return s_anim_en && dip_swap(swap, s_dip_out_ms); }

static void glance_exit(void)
{
    if (!s_glancing) return;
    s_glancing = false;
    if (s_cur == SCREEN_THERMOSTAT) refresh_thermostat();
}

static void sleep_dark(void)
{
    if (s_disp_asleep && s_on_disp_sleep) s_on_disp_sleep(true);
}

static void disp_sleep(void)
{
    if (s_disp_asleep) return;
    s_disp_asleep = true;
    home_stop_alert_rim();
    if (!dip_swap(sleep_dark, SLEEP_FADE_MS)) sleep_dark();
}

static void disp_wake(void)
{
    if (!s_disp_asleep) return;
    s_disp_asleep = false;
    if (s_refresh_pending) {
        s_refresh_pending = false;
        refresh_cur_screen();
    }
#if CONFIG_SERIN_BOARD_VIEWE_21
    if (s_on_disp_sleep) s_on_disp_sleep(false);
#else
    int fl = rest_target();
    if (s_on_bright) s_on_bright(fl);
    s_bright_cur = fl;
    if (s_on_disp_sleep) s_on_disp_sleep(false);
    if (!s_dimmed) bright_ramp_to(wake_target(), RAMP_UP_MS);
#endif
}

static volatile bool s_rendered;
static void render_ready_cb(lv_event_t *e) { (void)e; s_rendered = true; }
bool ui_has_rendered(void) { return s_rendered; }

#define LVGL_WDT_FEED_MS 1000
static void lvgl_wdt_cb(lv_timer_t *t)
{
    (void)t;
    static bool subscribed;
    if (!subscribed) {
        subscribed = (esp_task_wdt_add(NULL) == ESP_OK);
        if (!subscribed) return;
    }
    esp_task_wdt_reset();
}

static void idle_timer_cb(lv_timer_t *t)
{
    (void)t;
#if CONFIG_SERIN_THERMAL_CAL
    if (s_cal_owned) return;
#endif
    uint32_t idle = lv_display_get_inactive_time(NULL);
    bool may_dim = s_link.bonded ? (s_pair_state != PAIR_UI_SEARCHING)
                                 : (s_pair_state == PAIR_UI_HIDDEN);
    idle_set_dim(may_dim && idle >= IDLE_DIM_MS);

    if (idle >= IDLE_DIM_MS) child_lock_note_idle(&s_child_lock);

    bool gate_off    = s_screen_gate == UI_SCREEN_GATE_OFF;
    bool sleep_stage = gate_off ||
                       (s_screen_gate == UI_SCREEN_GATE_NONE && s_idle_off);
    uint32_t stage_ms = gate_off ? IDLE_DIM_MS : IDLE_GLANCE_MS;
    if (s_link.bonded && !s_glancing && !s_disp_asleep && idle >= stage_ms
        && s_pair_state == PAIR_UI_HIDDEN
        && !s_hold_timer) {
        if (dlg_is_open()) alert_dialog_hide();
        if (ui_coach_active()) coach_dismiss();
        if (sleep_stage) {
            if (s_cur != SCREEN_THERMOSTAT) ui_show_screen(SCREEN_THERMOSTAT);
            disp_sleep();
        } else {
            s_glancing  = true;
            s_glance_dx = (int)lv_rand(0, 2 * GLANCE_SHIFT_PX) - GLANCE_SHIFT_PX;
            s_glance_dy = (int)lv_rand(0, 2 * GLANCE_SHIFT_PX) - GLANCE_SHIFT_PX;
            if (s_cur != SCREEN_THERMOSTAT) ui_show_screen(SCREEN_THERMOSTAT);
            else                            refresh_thermostat();
        }
    }
}

void ui_set_screen_gate(ui_screen_gate_t gate)
{
    if (gate == s_screen_gate) return;
    s_screen_gate = gate;
    if (gate == UI_SCREEN_GATE_ON && s_disp_asleep) {
        s_glancing  = true;
        s_glance_dx = (int)lv_rand(0, 2 * GLANCE_SHIFT_PX) - GLANCE_SHIFT_PX;
        s_glance_dy = (int)lv_rand(0, 2 * GLANCE_SHIFT_PX) - GLANCE_SHIFT_PX;
        if (s_cur != SCREEN_THERMOSTAT) ui_show_screen(SCREEN_THERMOSTAT);
        else                            refresh_thermostat();
        disp_wake();
    } else if (gate == UI_SCREEN_GATE_OFF && s_glancing) {
        glance_exit();
        disp_sleep();
    }
}

void ui_set_night(bool night, uint8_t ceil_pct)
{
    s_night = night;
    s_night_ceil = ceil_pct;
}
bool ui_night_active(void)    { return s_night && s_night_en; }

/*                     base            glow            name         icon         running      idle         cap                       */
static const mode_info_t MODE_INFO[] = {
    [MODE_OFF]       = { COL_OFF_BASE,  COL_OFF_GLOW,  "Off",       &mode_off,  "On",        "On",        0                        },
    [MODE_HEAT]      = { COL_HEAT_BASE, COL_HEAT_GLOW, "Heat",      &mode_heat, "Heating",   "Heat",      1u << SL2_MODE_HEAT      },
    [MODE_COOL]      = { COL_COOL_BASE, COL_COOL_GLOW, "Cool",      &mode_cool, "Cooling",   "Cool",      1u << SL2_MODE_COOL      },
    [MODE_HEAT_COOL] = { COL_AUTO_BASE, COL_AUTO_GLOW, "Heat/Cool", &mode_auto, "Heat/Cool", "Heat/Cool", 1u << SL2_MODE_HEAT_COOL },
    [MODE_AUTO]      = { COL_AUTO_BASE, COL_AUTO_GLOW, "Auto",      &mode_auto, "Auto",      "Auto",      1u << SL2_MODE_AUTO      },
    [MODE_DRY]       = { COL_DRY_BASE,  COL_DRY_GLOW,  "Dry",       &mode_dry,  "Drying",    "Dry",       1u << SL2_MODE_DRY       },
    [MODE_FAN]       = { COL_FAN_BASE,  COL_FAN_GLOW,  "Fan",       &mode_fan,  "Fan",       "Fan",       1u << SL2_MODE_FAN_ONLY  },
};

const mode_info_t *mode_info(hvac_mode_t m)
{
    return &MODE_INFO[(unsigned)m <= MODE_FAN ? (unsigned)m : (unsigned)MODE_OFF];
}

void mode_pal(hvac_mode_t m, uint32_t *base, uint32_t *glow, const char **label)
{
    const mode_info_t *r = mode_info(m);
    *base = r->base; *glow = r->glow; *label = r->name;
}
uint32_t mode_accent(hvac_mode_t m)
{
    return (m == MODE_OFF) ? COL_GREY : mode_info(m)->glow;
}

const serin_icon_t *mode_icon(hvac_mode_t m) { return mode_info(m)->icon; }

const char *status_text(const hvac_state_t *s, bool active, bool off)
{
    if (off) return "Off";
    const mode_info_t *r = mode_info(s->mode);
    return active ? r->running : r->idle;
}

#define SPLASH_FADE_MS   400
#define SPLASH_HOLD_MS   700
#define SPLASH_LOGO_PX   160
#define SPLASH_GAP       20

static void splash_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void splash_finish(void)
{
    lv_obj_delete(s_splash);
    s_splash = NULL;
    coach_init();
}
static void splash_done_cb(lv_anim_t *a) { (void)a; splash_finish(); }

/* On a dimmable panel the logo is drawn once and the light does the fading. */
static void splash_dark(void)
{
    splash_finish();
    dip_relight_after_frame(SPLASH_FADE_MS, NULL);
}
static void splash_dim_cb(lv_timer_t *t)
{
    (void)t;
    dip_start(0, SPLASH_FADE_MS, splash_dark);
}
static void splash_lit(void)
{
    lv_timer_t *t = lv_timer_create(splash_dim_cb, SPLASH_HOLD_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void splash_show(void)
{
    lv_obj_t *ov = s_splash = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, SERIN_DISP, SERIN_DISP);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *grp = lv_obj_create(ov);
    lv_obj_remove_style_all(grp);
    lv_obj_set_size(grp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grp, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grp, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grp, SERIN_PX(SPLASH_GAP), 0);
    lv_obj_center(grp);

    mkicon(grp, &serin_logo, SERIN_PX(SPLASH_LOGO_PX), COL_SPLASH_LOGO);
    lv_obj_t *word = mklabel(grp, FONT_SET_NAME, COL_SPLASH_WORD, "serin labs");
    lv_obj_set_style_text_letter_space(word, SERIN_PX(1), 0);

    if (s_on_bright_scale) {
        s_dip_cur = 0;
        s_on_bright_scale(0);
        dip_relight_after_frame(SPLASH_FADE_MS, splash_lit);
        return;
    }

    lv_obj_set_style_opa(grp, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, grp);
    lv_anim_set_exec_cb(&a, splash_opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, SPLASH_FADE_MS);
    lv_anim_set_playback_delay(&a, SPLASH_HOLD_MS);
    lv_anim_set_playback_duration(&a, SPLASH_FADE_MS);
    lv_anim_set_completed_cb(&a, splash_done_cb);
    lv_anim_start(&a);
}

static void panel_click_cb(lv_event_t *e);
static void pill_click_cb(lv_event_t *e);
static void alert_click_cb(lv_event_t *e);
static void zchip_click_cb(lv_event_t *e);
static void airbtn_click_cb(lv_event_t *e);
static void airbtn_refresh(void);
static void pair_hint_paint(void);
static void pair_retry(void);
static void pair_dismiss(void);
static void pair_face_press(void);
static void pair_btn_cb(lv_event_t *e);
static void pair_btn2_cb(lv_event_t *e);

lv_obj_t *mkpanel(uint32_t face, bool clickToMenu)
{
    lv_obj_t *p = lv_obj_create(s_root);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, SERIN_DISP, SERIN_DISP);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_transform_pivot_x(p, SERIN_C, 0);
    lv_obj_set_style_transform_pivot_y(p, SERIN_C, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(face), 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    if (clickToMenu) {
        lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(p, panel_click_cb, LV_EVENT_CLICKED, NULL);
    }
    return p;
}

static touch_floor_arg_t s_pairbtn_tf  = { TF_CAP_PILL_ROW, "T.pairBtn"  };
static touch_floor_arg_t s_pairbtn2_tf = { TF_CAP_PILL_ROW, "T.pairBtn2" };

#define PAIR_ROW_DY        20
#define PAIR_ROW_DY_SEARCH  0
#define PAIR_ROW_DY_FAIL   32
#define PAIR_PILL_R        27
#define PAIR_PILL_PAD_H    29
#define PAIR_PILL_PAD_V    14
#define PAIR_PILL2_R       24
#define PAIR_PILL2_PAD_H   16
#define PAIR_PILL2_PAD_V   12

static void pair_pill_geom(lv_obj_t *pill, bool duo)
{
    lv_obj_set_style_radius(pill,    SERIN_PX(duo ? PAIR_PILL2_R     : PAIR_PILL_R),     0);
    lv_obj_set_style_pad_hor(pill,   SERIN_PX(duo ? PAIR_PILL2_PAD_H : PAIR_PILL_PAD_H), 0);
    lv_obj_set_style_pad_ver(pill,   SERIN_PX(duo ? PAIR_PILL2_PAD_V : PAIR_PILL_PAD_V), 0);
}

static void place_dot(lv_obj_t *o, int diam, float t)
{
    float a = serin_angle(t);
    int x = serin_px_x(a, DIAL_R);
    int y = serin_px_y(a, DIAL_R);
    lv_obj_set_pos(o, x - diam / 2, y - diam / 2);
}

void anim_bg_opa_exec(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0);
}

static int s_dispSet10 = 720;
#define HERO_TRACK    11
#define HERO_HALF_X   -8
static void hero_set_dd(int dd)
{
    lv_label_set_text_fmt(T.num, "%d", dd / 10);
    int frac = dd % 10; if (frac < 0) frac = -frac;
    if (frac) {
        lv_label_set_text_fmt(T.numHalf, ".%d", frac);
        lv_obj_remove_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align_to(T.numHalf, T.num, LV_ALIGN_OUT_RIGHT_BOTTOM, SERIN_PX(HERO_HALF_X), -SERIN_PX(24));
    lv_obj_align_to(T.deg, T.num, LV_ALIGN_OUT_RIGHT_TOP, SERIN_PX(4), SERIN_PX(18));
}
static uint8_t s_band_edge[UI_MAX_ZONES];
static inline int band_sel(void) { return s_band_edge[s_zactive]; }
static bool band_active(void)
{
    return s_state.mode == MODE_HEAT_COOL && s_state.band_valid;
}
#define BAND_NUM_OFS 100
#define BAND_TRACK    9
#define BAND_HALF_X   2
#define BAND_HALF_Y   10
static void band_hero_one(lv_obj_t *num, lv_obj_t *half, int dd, bool right)
{
    lv_label_set_text_fmt(num, "%d", dd / 10);
    lv_obj_align(num, LV_ALIGN_CENTER,
                 right ? SERIN_PX(BAND_NUM_OFS) : -SERIN_PX(BAND_NUM_OFS), 0);
    int frac = dd % 10; if (frac < 0) frac = -frac;
    if (frac) {
        lv_label_set_text_fmt(half, ".%d", frac);
        lv_obj_align_to(half, num, LV_ALIGN_OUT_RIGHT_BOTTOM, SERIN_PX(BAND_HALF_X), -SERIN_PX(BAND_HALF_Y));
        lv_obj_remove_flag(half, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(half, LV_OBJ_FLAG_HIDDEN);
}
#define BAND_INACT_OPA ((lv_opa_t)90)
static void band_heroes(void)
{
    band_hero_one(T.bandLo, T.bandLoHalf, s_state.set_lo_dd, false);
    band_hero_one(T.bandHi, T.bandHiHalf, s_state.set_hi_dd, true);
    bool hi = band_sel();
    lv_obj_t *act = hi ? T.bandHi : T.bandLo;
    uint32_t actCol = hi ? COL_COOL_GLOW : COL_HEAT_GLOW;

    bool remind = face_dismissed(thermo_face());
    lv_opa_t on  = remind ? REMIND_HERO_OPA : LV_OPA_COVER;
    lv_opa_t dim = remind ? REMIND_HERO_OPA : BAND_INACT_OPA;

    lv_obj_set_style_text_opa(T.bandLo,     hi ? dim : on, 0);
    lv_obj_set_style_text_opa(T.bandLoHalf, hi ? dim : on, 0);
    lv_obj_set_style_text_opa(T.bandHi,     hi ? on : dim, 0);
    lv_obj_set_style_text_opa(T.bandHiHalf, hi ? on : dim, 0);

    lv_obj_set_style_border_color(T.bandRing, lv_color_hex(actCol), 0);
    lv_obj_align_to(T.bandRing, act, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(T.bandRing);
    lv_obj_align(T.bandSep, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(T.bandRing, remind ? REMIND_HERO_OPA : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(T.bandSep,  remind ? REMIND_HERO_OPA : LV_OPA_COVER, 0);
}
static void band_show(bool on)
{
    lv_obj_t *objs[] = { T.bandLo, T.bandHi, T.bandSep, T.bandRing,
                         T.bandTapL, T.bandTapR };
    for (size_t i = 0; i < sizeof objs / sizeof objs[0]; i++) {
        if (on) lv_obj_remove_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        else    lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!on) {
        lv_obj_add_flag(T.bandLoHalf, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.bandHiHalf, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.setDot2,    LV_OBJ_FLAG_HIDDEN);
    }
}

static int *edit_slot(void)
{
    if (!band_active()) return &s_state.set_dd;
    return band_sel() ? &s_state.set_hi_dd : &s_state.set_lo_dd;
}
static inline int active_edit_dd(void) { return *edit_slot(); }
static void edit_rails(int *lo, int *hi)
{
    *lo = dial_ddmin(); *hi = dial_ddmax();
    if (!band_active() || !s_caps.band_min_gap_dc) return;
    bool use_f = !s_units_c;
    int  gap   = s_caps.band_min_gap_dc;
    if (band_sel()) {
        int16_t min_dc = (int16_t)(ui_dd_to_dc(s_state.set_lo_dd, use_f, s_caps.ftab_id) + gap);
        int dd = ui_dd_from_dc(min_dc, use_f, s_caps.ftab_id);
        while (dd <= dial_ddmax() &&
               ui_dd_to_dc(dd, use_f, s_caps.ftab_id) < min_dc) dd += dial_step_dd();
        if (dd > *lo) *lo = dd;
    } else {
        int16_t max_dc = (int16_t)(ui_dd_to_dc(s_state.set_hi_dd, use_f, s_caps.ftab_id) - gap);
        int dd = ui_dd_from_dc(max_dc, use_f, s_caps.ftab_id);
        while (dd >= dial_ddmin() &&
               ui_dd_to_dc(dd, use_f, s_caps.ftab_id) > max_dc) dd -= dial_step_dd();
        if (dd < *hi) *hi = dd;
    }
}

static void thermo_ring(float setdd);

/* The setpoints are display values. Carry them into the new unit before the
 * controller echoes it, or the next step starts from an old-unit number and
 * clamps to a rail. A flip straight back restores the exact old values.
 * Band edges are not re-separated: edges closer than one step of the new
 * unit can land on the same value (lo == hi). Not fixed here. */
void units_set(bool units_c)
{
    static ui_dd_flip_t memo[3];
    static int memo_zone;
    if (units_c == s_units_c) return;
    /* Another zone's flip must not restore into this zone. */
    if (memo_zone != s_zactive) { memset(memo, 0, sizeof memo); memo_zone = s_zactive; }
    bool was_f = !s_units_c;
    s_units_c = units_c;
    s_state.use_f = !units_c;
    int *dd[] = { &s_state.set_dd, &s_state.set_lo_dd, &s_state.set_hi_dd };
    for (size_t i = 0; i < sizeof dd / sizeof *dd; i++) {
        if (*dd[i] <= 0) continue;
        int v = ui_dd_flip(&memo[i], *dd[i], was_f, !units_c, s_caps.ftab_id);
        if (v < dial_ddmin()) v = dial_ddmin();
        if (v > dial_ddmax()) v = dial_ddmax();
        *dd[i] = memo[i].after = v;
    }
    notify_units(!units_c);
}

static bool dial_gesture_tail(void)
{
    return s_dial_gest && !uig_click_is_tap(s_dial_gest);
}

static bool lock_refuse(void);

static void band_edge_click_cb(lv_event_t *e)
{
    if (dial_gesture_tail()) return;
    note_activity();
    if (lock_refuse()) return;
    s_band_edge[s_zactive] = (lv_event_get_user_data(e) != NULL);
    band_heroes();
    s_dispSet10 = active_edit_dd();
    thermo_ring((float)s_dispSet10);
}

static void arc_set_span(float tl, float th)
{
    if (th - tl < 0.012f) th = tl + 0.012f;
    int aMin = serin_lv_angle(serin_angle(tl));
    int aMax = serin_lv_angle(serin_angle(th));
    lv_arc_set_angles(T.arc, ((aMin % 360) + 360) % 360, ((aMax % 360) + 360) % 360);
}
static void thermo_ring(float setdd)
{
    const hvac_state_t *s = &s_state;
    if (band_active()) {
        float lodd = band_sel() ? (float)s->set_lo_dd : setdd;
        float hidd = band_sel() ? setdd : (float)s->set_hi_dd;
        float tl = serin_temp_t(lodd, dial_ddmin(), dial_ddmax());
        float th = serin_temp_t(hidd, dial_ddmin(), dial_ddmax());
        arc_set_span(tl, th);
        place_dot(T.setDot,  2 * SERIN_PX(SERIN_DOT_SET_R), tl);
        place_dot(T.setDot2, 2 * SERIN_PX(SERIN_DOT_SET_R), th);
        return;
    }
    float rp = serin_temp_t((float)(s->room_temp * 10), dial_ddmin(), dial_ddmax());
    float sp = serin_temp_t(setdd, dial_ddmin(), dial_ddmax());
    arc_set_span(rp < sp ? rp : sp, rp > sp ? rp : sp);
    place_dot(T.setDot, 2 * SERIN_PX(SERIN_DOT_SET_R), sp);
}
static void thermo_layout(int setdd)
{
    thermo_ring((float)setdd);
    place_dot(T.roomDot, 2 * SERIN_PX(SERIN_DOT_ROOM_R),
              serin_temp_t((float)(s_state.room_temp * 10), dial_ddmin(), dial_ddmax()));
    if (band_active()) band_heroes();
    else               hero_set_dd(setdd);
}
static void thermo_ring_exec(void *u, int32_t vdd) { (void)u; s_dispSet10 = vdd; thermo_ring((float)vdd); }

static bool    s_batt_was_low  [UI_MAX_ZONES];
static int64_t s_batt_snooze_to[UI_MAX_ZONES];
static bool    s_chip_is_batt = false;
#define BATT_SNOOZE_US (24LL * 60 * 60 * 1000000)
static bool    s_wifi_dismissed[UI_MAX_ZONES];

static const char *wifi_skip_key(char buf[16], const uint8_t *mac)
{
    if (!mac) return "wifi_skip";
    snprintf(buf, 16, "ws_%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}
static const uint8_t *zactive_mac(void) { return s_zn_ui ? s_zinfo[s_zactive].mac : NULL; }
static bool s_link_dismissed [UI_MAX_ZONES];
static bool s_cn105_dismissed[UI_MAX_ZONES];

bool face_dismissed(dial_face_t f)
{
    return (f == DIAL_LINK_OFF  && s_link_dismissed [s_zactive]) ||
           (f == DIAL_CN105_OFF && s_cn105_dismissed[s_zactive]);
}

bool home_face_live(void) { return thermo_face() == DIAL_LIVE; }

bool home_lock_gates(void)
{
    dial_face_t f = thermo_face();
    child_lock_face_t lf = face_dismissed(f)    ? CHILD_LOCK_FACE_REMIND
                         : f == DIAL_LINK_OFF  ? CHILD_LOCK_FACE_LINK_OFF
                         : f == DIAL_CN105_OFF ? CHILD_LOCK_FACE_CN105_OFF
                         : f == DIAL_WIFI_OFF  ? CHILD_LOCK_FACE_WIFI_OFF
                         : f == DIAL_UNPAIRED  ? CHILD_LOCK_FACE_UNPAIRED
                                               : CHILD_LOCK_FACE_LIVE;
    return child_lock_gates(&s_child_lock, lf);
}

bool input_quiet(void)
{
    if (s_btn_down || s_hold_timer) return false;
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED) return false;
    return true;
}

void home_batt_dismiss_24h(void)
{
    s_batt_snooze_to[s_zactive] = esp_timer_get_time() + BATT_SNOOZE_US;
    s_chip_is_batt = false;
}
bool batt_snoozed(void)
{
    return esp_timer_get_time() < s_batt_snooze_to[s_zactive];
}

static void edit_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_edit = false;
    s_edit_timer = NULL;
    if (s_cur == SCREEN_THERMOSTAT) refresh_thermostat();
}

#define SYNC_TELL_MIN_MS 1000
static uint32_t    s_sync_tell_ms;
static lv_timer_t *s_sync_tell_timer;
static void sync_tell_timer_cb(lv_timer_t *t)
{
    (void)t;
    s_sync_tell_timer = NULL;
    if (s_cur == SCREEN_THERMOSTAT) refresh_thermostat();
}

static void set_setpoint(int dd, bool animate)
{
    int rlo, rhi; edit_rails(&rlo, &rhi);
    if (rlo > rhi) dd = *edit_slot();
    else {
        if (dd < rlo) dd = rlo;
        if (dd > rhi) dd = rhi;
    }
    *edit_slot() = dd;
    s_edit = true;
    if (s_edit_timer) lv_timer_reset(s_edit_timer);
    else s_edit_timer = lv_timer_create(edit_timer_cb, 1200, NULL);
    lv_timer_set_repeat_count(s_edit_timer, 1);
    if (band_active()) {
        bool hi = band_sel();
        band_hero_one(hi ? T.bandHi : T.bandLo,
                      hi ? T.bandHiHalf : T.bandLoHalf, dd, hi);
    } else {
        hero_set_dd(dd);
        lv_obj_set_style_text_color(T.num, lv_color_hex(mode_accent(s_state.mode)), 0);
        lv_obj_set_style_text_color(T.numHalf, lv_color_hex(mode_accent(s_state.mode)), 0);
    }
    if (animate) {
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, T.arc);
        lv_anim_set_exec_cb(&a, thermo_ring_exec);
        lv_anim_set_values(&a, s_dispSet10, dd);
        lv_anim_set_duration(&a, 160);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        s_dispSet10 = dd;
        thermo_ring((float)dd);
    }
}

static inline float dial_deg_per_step(void) {
    return SERIN_ARC_SWEEP * (float)dial_step_dd()
         / (float)(dial_ddmax() - dial_ddmin());
}

static float dial_point_angle(int px, int py)
{
    return atan2f((float)(px - SERIN_C), (float)(SERIN_C - py)) / SERIN_DEG;
}
static float dial_point_radius(int px, int py)
{
    float dx = (float)(px - SERIN_C), dy = (float)(py - SERIN_C);
    return sqrtf(dx * dx + dy * dy);
}
static float wrap180(float d)
{
    while (d >  180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static bool swipe_gate_open(void)
{
    if (s_zn_ui < 2) return false;
    dial_face_t f = thermo_face();
    return f == DIAL_LIVE || f == DIAL_LINK_OFF || f == DIAL_CN105_OFF;
}

static bool lock_gates(void)
{
    return s_cur == SCREEN_THERMOSTAT && home_lock_gates();
}

static bool lock_refuse(void)
{
    if (!lock_gates()) return false;
    child_lock_pill_flash();
    return true;
}

static bool dial_drag_arm(lv_point_t p)
{
    s_drag_engaged  = false;
    s_drag_anchored = false;
    s_drag_accum    = 0.0f;
    if (thermo_face() != DIAL_LIVE) return false;
    if (s_state.alert == ALERT_SYNCING) return false;
    if (dial_point_radius(p.x, p.y) >= SERIN_DRAG_R_MIN) {
        s_drag_ang0 = s_drag_ang = dial_point_angle(p.x, p.y);
        s_drag_anchored = true;
    }
    return true;
}

static bool dial_drag_frame(lv_point_t p, bool may_engage)
{
    if (dial_point_radius(p.x, p.y) < SERIN_DRAG_R_MIN) return s_drag_engaged;
    float ang = dial_point_angle(p.x, p.y);
    if (!s_drag_anchored) {
        s_drag_ang0 = s_drag_ang = ang;
        s_drag_anchored = true;
        return s_drag_engaged;
    }

    float d = wrap180(ang - s_drag_ang);
    s_drag_ang = ang;
    if (d > SERIN_DRAG_CLAMP_DEG || d < -SERIN_DRAG_CLAMP_DEG) return s_drag_engaged;

    if (!s_drag_engaged && may_engage) {
        float from_anchor = wrap180(ang - s_drag_ang0);
        if (from_anchor > SERIN_DRAG_ENGAGE_DEG || from_anchor < -SERIN_DRAG_ENGAGE_DEG) {
            if (lock_refuse()) return false;
            s_drag_engaged = true;
            note_activity();
        }
    }
    if (!s_drag_engaged) return false;

    s_drag_accum += d / dial_deg_per_step();
    bool changed = false;
    while (s_drag_accum >=  1.0f) {
        if (!coach_demo_turn(1))  { set_setpoint(*edit_slot() + dial_step_dd(), false); changed = true; }
        s_drag_accum -= 1.0f;
    }
    while (s_drag_accum <= -1.0f) {
        if (!coach_demo_turn(-1)) { set_setpoint(*edit_slot() - dial_step_dd(), false); changed = true; }
        s_drag_accum += 1.0f;
    }
    if (!coach_sandbox_active()) {
        int rlo, rhi; edit_rails(&rlo, &rhi);
        if      (*edit_slot() >= rhi && s_drag_accum > 0) s_drag_accum = 0.0f;
        else if (*edit_slot() <= rlo && s_drag_accum < 0) s_drag_accum = 0.0f;
    }

    if (changed && lv_tick_elaps(s_drag_last_tx) >= SERIN_DRAG_TX_MS) {
        notify_edit();
        s_drag_last_tx = lv_tick_get();
    }
    return true;
}

static void dial_drag_end(void)
{
    if (coach_sandbox_active()) return;
    notify_edit();
}

static void dial_swipe(int dir)
{
    note_activity();
    if (lock_refuse()) return;
    zone_swipe(dir);
}

static bool swipe_v_gate_open(void)
{
    if (thermo_face() != DIAL_LIVE) return false;
    if (s_state.alert == ALERT_SYNCING) return false;
    return airflow_available();
}

static void dial_swipe_v(int dir)
{
    if (dir <= 0) return;
    note_activity();
    if (lock_refuse()) return;
    ui_show_screen(SCREEN_FANVANE);
}

static bool dial_press_eats(void) { return dim_visible() || s_glancing || s_disp_asleep; }
static void dial_wake(void)       { note_activity(); glance_exit(); disp_wake(); }

void face_dismiss(dial_face_t face)
{
    switch (face) {
        case DIAL_WIFI_OFF: {
            char kb[16];
            s_wifi_dismissed[s_zactive] = true;
            prefs_set_bool(wifi_skip_key(kb, zactive_mac()), true);
            break;
        }
        case DIAL_LINK_OFF:  s_link_dismissed [s_zactive] = true;   break;
        case DIAL_CN105_OFF: s_cn105_dismissed[s_zactive] = true;   break;
        default: return;
    }
    refresh_thermostat();
}

static bool dial_hold(void)
{
    if (lock_gates()) return false;
    if (!swipe_gate_open()) return false;
    note_activity();
    ui_show_screen(SCREEN_ZONES);
    return true;
}

static const uig_cfg_t DIAL_GESTURES = {
    .swipe_min_px    = SERIN_SWIPE_MIN_PX,
    .swipe_dial_px   = SERIN_SWIPE_DIAL_PX,
    .swipe_dom       = SERIN_SWIPE_DOM,
    .swipe_window_ms = SERIN_SWIPE_WINDOW_MS,
    .swipe_gate      = swipe_gate_open,
    .on_swipe        = dial_swipe,
    .swipe_v_gate    = swipe_v_gate_open,
    .on_swipe_v      = dial_swipe_v,
    .drag_arm        = dial_drag_arm,
    .on_drag_frame   = dial_drag_frame,
    .on_drag_end     = dial_drag_end,
    .on_hold         = dial_hold,
    .press_eats      = dial_press_eats,
    .on_eaten        = dial_wake,
    .on_press        = ui_button_down,
    .on_release      = ui_button_up,
};

static lv_obj_t *mkplate(lv_obj_t *p, int radius, lv_opa_t opa,
                         int pad_h, int pad_v, int pad_col)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(o, SERIN_PX(radius), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_pad_hor(o, SERIN_PX(pad_h), 0);
    lv_obj_set_style_pad_ver(o, SERIN_PX(pad_v), 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (pad_col) lv_obj_set_style_pad_column(o, SERIN_PX(pad_col), 0);
    return o;
}

static void build_thermostat(void)
{
    lv_obj_t *p = s_panel[SCREEN_THERMOSTAT] = mkpanel(COL_FACE, true);

    s_dial_gest = uig_attach(p, &DIAL_GESTURES);

    lv_obj_t *arc = T.arc = lv_arc_create(p);
    int d = 2 * SERIN_PX(DIAL_R) + SERIN_PX(SERIN_TRACK_W);
    lv_obj_set_size(arc, d, d);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, SERIN_PX(SERIN_TRACK_W), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, SERIN_PX(SERIN_TRACK_W), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_obj_t *ring = T.nolinkRing = arim_ring_create(p, &s_arim_home);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);

    {
        lv_obj_t *ha = T.holdArc = lv_arc_create(p);
        lv_obj_set_size(ha, d, d);
        lv_obj_center(ha);
        lv_arc_set_rotation(ha, 0);
        lv_arc_set_bg_angles(ha, ARIM_ARC_A0, ARIM_ARC_A0);
        lv_obj_remove_style(ha, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(ha, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_opa(ha, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(ha, SERIN_PX(SERIN_TRACK_W), LV_PART_MAIN);
        lv_obj_set_style_arc_color(ha, lv_color_hex(COL_PURE), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(ha, true, LV_PART_MAIN);
    }
    lv_obj_add_flag(T.holdArc, LV_OBJ_FLAG_HIDDEN);

    T.roomDot = mkdot(p, 2 * SERIN_PX(SERIN_DOT_ROOM_R), COL_PURE);
    T.setDot  = mkdot(p, 2 * SERIN_PX(SERIN_DOT_SET_R),  COL_PURE);
    T.setDot2 = mkdot(p, 2 * SERIN_PX(SERIN_DOT_SET_R),  COL_PURE);
    lv_obj_add_flag(T.setDot2, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *const kl_dots[] = { T.roomDot, T.setDot, T.setDot2 };
    for (unsigned i = 0; i < sizeof(kl_dots) / sizeof(kl_dots[0]); i++) {
        lv_obj_set_style_border_width(kl_dots[i], SERIN_PX(SERIN_DOT_KEYLINE), 0);
        lv_obj_set_style_border_color(kl_dots[i], lv_color_hex(COL_FACE), 0);
        lv_obj_set_style_border_opa(kl_dots[i], LV_OPA_COVER, 0);
    }

#define PILL_TOP 72
    lv_obj_t *pill = T.pill = mkplate(p, 30, LV_OPA_20, 18, 7, 8);
    lv_obj_align(pill, LV_ALIGN_TOP_MID, 0, SERIN_PX(PILL_TOP));
    lv_obj_add_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(pill, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(pill, pill_click_cb, LV_EVENT_CLICKED, NULL);
    press_ack(pill, ACK_OPA_PILL);
    T.status = mklabel(pill, FONT_DIAL_MODE, COL_HEAT_GLOW, "Heating");
    static const touch_floor_arg_t k_pill_tf = { 0, "T.pill" };
    touch_floor(pill, &k_pill_tf);

#define CHIP_BOT_Y     40
#define ZCHIP_NAME_MAX     240
#define ZCHIP_IDLE_OPA LV_OPA_20
#define ALERT_BG_OPA   13
    lv_obj_t *zc = T.zchip = mkplate(p, 30, ZCHIP_IDLE_OPA, 18, 7, 8);
    lv_obj_align(zc, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(CHIP_BOT_Y));
    lv_obj_add_flag(zc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(zc, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(zc, zchip_click_cb, LV_EVENT_CLICKED, NULL);
    press_ack(zc, ACK_OPA_PILL);
    T.zname = mklabel(zc, FONT_DIAL_PILL, COL_ZNAME, "");
    lv_obj_set_style_max_width(T.zname, SERIN_PX(ZCHIP_NAME_MAX), 0);
    lv_obj_set_style_max_height(T.zname, lv_font_get_line_height(FONT_DIAL_PILL), 0);
    lv_label_set_long_mode(T.zname, LV_LABEL_LONG_DOT);
    static const touch_floor_arg_t k_zc_tf = { 0, "T.zchip" };
    touch_floor(zc, &k_zc_tf);
    lv_obj_add_flag(zc, LV_OBJ_FLAG_HIDDEN);

#define AIRBTN_SIZE   60
#define AIRBTN_GLYPH  40
#define AIRBTN_BOT_Y  16
    lv_obj_t *ab = T.airbtn = mkplate(p, AIRBTN_SIZE / 2, LV_OPA_30, 0, 0, 0);
    lv_obj_set_size(ab, SERIN_PX(AIRBTN_SIZE), SERIN_PX(AIRBTN_SIZE));
    lv_obj_set_style_bg_color(ab, lv_color_hex(COL_SYSTEM), 0);
    lv_obj_align(ab, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(AIRBTN_BOT_Y));
    lv_obj_add_flag(ab, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ab, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(ab, airbtn_click_cb, LV_EVENT_CLICKED, NULL);
    press_ack(ab, ACK_OPA_PILL);
    mkicon(ab, &mode_fan, SERIN_PX(AIRBTN_GLYPH), COL_SYSTEM);
    static const touch_floor_arg_t k_ab_tf = { 0, "T.airbtn" };
    touch_floor(ab, &k_ab_tf);
    lv_obj_add_flag(ab, LV_OBJ_FLAG_HIDDEN);

    T.num = mklabel(p, FONT_DIAL_HERO, COL_WHITE, "72");
    lv_obj_set_style_text_letter_space(T.num, -SERIN_PX(HERO_TRACK), 0);
    lv_obj_align(T.num, LV_ALIGN_CENTER, 0, 0);
    T.deg = mklabel(p, FONT_DIAL_DEG, COL_TEXT_SOFT, "\xC2\xB0");
    lv_obj_set_style_text_opa(T.deg, HERO_DEG_OPA, 0);
    T.numHalf = mklabel(p, FONT_SSID, COL_WHITE, ".5");
    lv_obj_add_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN);

#define BAND_RING_W  184
#define BAND_RING_H  172
#define BAND_RING_R  40
    for (int side = 0; side < 2; side++) {
        lv_obj_t *tap = lv_obj_create(p);
        lv_obj_remove_style_all(tap);
        lv_obj_set_size(tap, SERIN_PX(BAND_RING_W), SERIN_PX(BAND_RING_H));
        lv_obj_set_style_radius(tap, SERIN_PX(BAND_RING_R), 0);
        lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
        lv_obj_align(tap, LV_ALIGN_CENTER,
                     side ? SERIN_PX(BAND_NUM_OFS) : -SERIN_PX(BAND_NUM_OFS), 0);
        lv_obj_add_flag(tap, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(tap, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(tap, band_edge_click_cb, LV_EVENT_CLICKED,
                            side ? (void *)1 : NULL);
        press_ack(tap, ACK_OPA_TILE);
        if (side) T.bandTapR = tap; else T.bandTapL = tap;
    }
    T.bandLo  = mklabel(p, FONT_BAND_HERO, COL_HEAT_GLOW, "68");
    T.bandHi  = mklabel(p, FONT_BAND_HERO, COL_COOL_GLOW, "74");
    lv_obj_set_style_text_letter_space(T.bandLo, -SERIN_PX(BAND_TRACK), 0);
    lv_obj_set_style_text_letter_space(T.bandHi, -SERIN_PX(BAND_TRACK), 0);
    T.bandLoHalf = mklabel(p, FONT_BAND_HALF, COL_HEAT_GLOW, ".5");
    T.bandHiHalf = mklabel(p, FONT_BAND_HALF, COL_COOL_GLOW, ".5");
    T.bandSep = mkdot(p, SERIN_PX(8), COL_BAND_SEP);
    T.bandRing = lv_obj_create(p);
    lv_obj_remove_style_all(T.bandRing);
    lv_obj_set_size(T.bandRing, SERIN_PX(BAND_RING_W), SERIN_PX(BAND_RING_H));
    lv_obj_set_style_radius(T.bandRing, SERIN_PX(BAND_RING_R), 0);
    lv_obj_set_style_bg_opa(T.bandRing, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(T.bandRing, SERIN_PX(3), 0);
    lv_obj_set_style_border_opa(T.bandRing, (lv_opa_t)90, 0);
    lv_obj_remove_flag(T.bandSep,  LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(T.bandRing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(T.bandLo,   LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(T.bandHi,   LV_OBJ_FLAG_CLICKABLE);
    band_show(false);

    lv_obj_t *now = T.now = mkplate(p, 30, LV_OPA_TRANSP, 18, 7, 0);
    lv_obj_align(now, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(100));
    lv_obj_remove_flag(now, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *nowlbl = mklabel(now, FONT_DIAL_NOW, COL_TEXT_SOFT, "Now 69\xC2\xB0 \xC2\xB7 44%");
    lv_obj_set_style_text_opa(nowlbl, NOW_TXT_OPA, 0);
    lv_obj_set_user_data(now, nowlbl);

    lv_obj_t *al = T.alert = mkplate(p, 24, ALERT_BG_OPA, 17, 9, 9);
    lv_obj_set_style_border_width(al, 1, 0);
    lv_obj_align(al, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(CHIP_BOT_Y));
    lv_obj_add_flag(al, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(al, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(al, alert_click_cb, LV_EVENT_CLICKED, NULL);
    press_ack(al, ACK_OPA_CHIP);
    T.alertIcon = mkicon(al, &hero_alert, SERIN_PX(16), COL_ALERT_NOLINK);
    T.alertTxt = mklabel(al, FONT_DIAL_CHIP, COL_ALERT_NOLINK, "NO LINK");
    static const touch_floor_arg_t k_al_tf = { 0, "T.alert" };
    touch_floor(al, &k_al_tf);
    lv_obj_add_flag(al, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lp = T.lockPill = mkplate(p, 30, LV_OPA_30, 18, 7, 8);
    lv_obj_set_style_bg_color(lp, lv_color_hex(COL_SYSTEM), 0);
    lv_obj_align(lp, LV_ALIGN_TOP_MID, 0, SERIN_PX(PILL_TOP));
    lv_obj_remove_flag(lp, LV_OBJ_FLAG_CLICKABLE);
    mkicon(lp, &set_lock, SERIN_PX(26), COL_WHITE);
    mklabel(lp, FONT_DIAL_PILL, COL_WHITE, H_HOLD("unlock"));
    lv_obj_add_flag(lp, LV_OBJ_FLAG_HIDDEN);

    T.pairIcon = mkicon(p, &serin_logo, SERIN_PX(130), COL_HEAT_GLOW);
    lv_obj_align(T.pairIcon, LV_ALIGN_TOP_MID, 0, SERIN_PX(58));
    lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);

    T.offIcon = mkicon(p, &hero_alert, SERIN_PX(32), COL_ALERT_NOLINK);
    lv_obj_align(T.offIcon, LV_ALIGN_CENTER, 0, -SERIN_PX(78));
    lv_obj_add_flag(T.offIcon, LV_OBJ_FLAG_HIDDEN);

    T.wifiHint = mklabel(p, FONT_STATUS_SM, COL_DIM, "");
    lv_obj_set_style_text_align(T.wifiHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(T.wifiHint, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(CHIP_BOT_Y));
    lv_obj_add_flag(T.wifiHint, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *pbx = T.pairBox = mkcol(p, SERIN_PX(360), 16, SERIN_PX(20));
    lv_obj_align(pbx, LV_ALIGN_TOP_MID, 0, SERIN_PX(220));
    lv_obj_add_flag(pbx, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pbx, LV_OBJ_FLAG_EVENT_BUBBLE);

    T.pairTitle = mklabel(pbx, FONT_DIAL_OFFLINE, COL_WHITE, "Ready to pair");
    lv_obj_set_style_text_align(T.pairTitle, LV_TEXT_ALIGN_CENTER, 0);

    T.pairInstr = mkbody(pbx, FONT_STATUS_SM, COL_DIM, "", SERIN_PX(330));

    T.pairHint = mklabel(pbx, FONT_STATUS_SM, COL_DIM, "");
    lv_obj_set_style_text_align(T.pairHint, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *prow = T.pairRow = mkpillrow(pbx, TF_GAP_PILL_ROW, SERIN_PX(PAIR_ROW_DY));
    lv_obj_remove_flag(prow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(prow, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *pbtn = T.pairBtn = mkpill(prow, "Pair", COL_ON_ACCENT, 27, 29, 14);
    T.pairBtnLbl = lv_obj_get_child(pbtn, 0);
    lv_obj_set_style_bg_opa(pbtn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pbtn, lv_color_hex(COL_HEAT_GLOW), 0);
    lv_obj_add_flag(pbtn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(pbtn, pair_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(pbtn, lv_color_hex(COL_HEAT_BASE), LV_PART_MAIN | LV_STATE_PRESSED);
    touch_floor(pbtn, &s_pairbtn_tf);

    lv_obj_t *pbtn2 = T.pairBtn2 = mkpill(prow, "Back", COL_WHITE, 27, 29, 14);
    lv_obj_set_style_bg_color(pbtn2, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pbtn2, OPA_FILL_NEUTRAL, 0);
    lv_obj_add_flag(pbtn2, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(pbtn2, pair_btn2_cb, LV_EVENT_CLICKED, NULL);
    press_ack(pbtn2, ACK_OPA_CHIP);
    touch_floor(pbtn2, &s_pairbtn2_tf);
    lv_obj_add_flag(pbtn2, LV_OBJ_FLAG_HIDDEN);
}

void fault_code_str(char *buf, size_t n, uint16_t code)
{
    snprintf(buf, n, code > 0xFF ? "0x%04X" : "0x%02X", (unsigned)code);
}

static dial_face_t thermo_face(void)
{
    const hvac_state_t *s = &s_state;
    if (s_pair_state != PAIR_UI_HIDDEN) return DIAL_UNPAIRED;
    if (!s_link.bonded)            return DIAL_UNPAIRED;
    if (s->alert == ALERT_NOLINK)  return DIAL_LINK_OFF;
    if (s->alert == ALERT_NONE && s->wifi_setup_avail && !s->wifi_provisioned &&
        !s_wifi_dismissed[s_zactive])
        return DIAL_WIFI_OFF;
    if (s->alert == ALERT_NONE && !s->cn105_connected) return DIAL_CN105_OFF;
    return DIAL_LIVE;
}

static bool face_is_blank(dial_face_t f)
{
    if (f == DIAL_LIVE) return false;
    if (face_dismissed(f)) return false;
    return true;
}

typedef struct { dial_face_t face; bool blank, remind, glance, syncing; } face_mode_t;

static void pair_badge(const serin_icon_t *ic, int px, uint32_t col)
{
    icon_fit(T.offIcon, ic, SERIN_PX(px));
    lv_obj_set_style_image_recolor(T.offIcon, lv_color_hex(col), 0);
    lv_obj_remove_flag(T.offIcon, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_sync_skeleton(void)
{
    if (!lock_pill_owns_slot()) lv_obj_remove_flag(T.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(T.pill, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(T.pill, lv_color_white(), 0);
    lv_obj_set_style_border_width(T.pill, 0, 0);
    lv_obj_set_style_text_font(T.status, FONT_DIAL_MODE, 0);
    lv_obj_set_style_text_align(T.status, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(T.pill, LV_ALIGN_TOP_MID, 0, SERIN_PX(PILL_TOP));
    lv_obj_set_style_text_color(T.status, lv_color_hex(COL_LINK), 0);
    lv_label_set_text(T.status, "Syncing");
    lv_obj_add_flag(T.now,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.alert,   LV_OBJ_FLAG_HIDDEN);
    s_chip_is_batt = false;
    lv_obj_add_flag(T.num,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.deg,     LV_OBJ_FLAG_HIDDEN);
    band_show(false);
    lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.pairBox,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.offIcon,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_opa(T.arc, LV_OPA_COVER,  LV_PART_MAIN);
    lv_obj_set_style_arc_opa(T.arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    arim_stop(T.nolinkRing);
    zchip_refresh();
}

static void refresh_glance(const hvac_state_t *s)
{
    s_chip_is_batt = false;
    lv_obj_add_flag(T.zchip,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.airbtn,  LV_OBJ_FLAG_HIDDEN);
    band_show(false);
    lv_obj_set_style_arc_opa(T.arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(T.arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    arim_stop(T.nolinkRing);

    lv_label_set_text_fmt(T.num, "%d", s->room_temp);
    lv_obj_set_style_text_color(T.num, lv_color_hex(COL_WHITE), 0);
    lv_obj_set_style_text_opa(T.num, GLANCE_HERO_OPA, 0);
    lv_obj_align(T.num, LV_ALIGN_CENTER, s_glance_dx, s_glance_dy);
    lv_obj_remove_flag(T.num, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(T.deg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_opa(T.deg, HERO_DEG_OPA, 0);
    lv_obj_align_to(T.deg, T.num, LV_ALIGN_OUT_RIGHT_TOP, SERIN_PX(4), SERIN_PX(18));

    lv_obj_set_style_bg_color(T.setDot, lv_color_hex(mode_accent(s->mode)), 0);
    place_dot(T.setDot, 2 * SERIN_PX(SERIN_DOT_SET_R),
              serin_temp_t((float)active_edit_dd(), dial_ddmin(), dial_ddmax()));
    lv_obj_remove_flag(T.setDot, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_blank_offline(dial_face_t face)
{
    lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(T.pairBox,  LV_OBJ_FLAG_HIDDEN);
    const char *label; uint32_t col;
    bool alertBadge;
    bool breathe;
    /* Copy convention: alert chips and takeovers are ALL CAPS; titles are sentence case. */
    switch (face) {
        case DIAL_LINK_OFF:  label = "CONTROLLER\nOFFLINE";  col = COL_ALERT_NOLINK;    alertBadge = true;  breathe = false; break;
        case DIAL_CN105_OFF: label = "HEAT PUMP\nOFFLINE"; col = COL_ALERT_WARN;  alertBadge = true;  breathe = true;  break;
        default:             label = "Set up Wi-Fi";  col = SET_COL[SET_WIFI];   alertBadge = true;  breathe = true;  break;
    }
    pair_badge(face == DIAL_WIFI_OFF ? &hero_hotspot : &hero_alert,
               face == DIAL_WIFI_OFF ? 80 : 32, col);
    if (face == DIAL_WIFI_OFF) {
        lv_label_set_text(T.wifiHint, H2(H_PRESS("start"), H_HOLD("skip")));
        lv_obj_remove_flag(T.wifiHint, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_remove_flag(T.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(T.pill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(T.pill, 0, 0);
    lv_obj_set_style_text_font(T.status, FONT_DIAL_OFFLINE, 0);
    lv_obj_set_style_text_align(T.status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(T.status, lv_color_hex(col), 0);
    lv_label_set_text(T.status, label);
    lv_obj_align(T.pill, LV_ALIGN_CENTER, 0, alertBadge ? SERIN_PX(14) : 0);

    s_arim_home.col = col;
    if (breathe) arim_breath_start(T.nolinkRing);
    else         arim_sweep_start(T.nolinkRing);
    lv_obj_remove_flag(T.nolinkRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(T.nolinkRing);
}

static void refresh_pairing_face(void)
{
    lv_obj_remove_flag(T.pairBox,  LV_OBJ_FLAG_HIDDEN);

    bool searching = (s_pair_state == PAIR_UI_SEARCHING);
    bool ok        = (s_pair_state == PAIR_UI_OK || s_pair_state == PAIR_UI_SAVED);
    bool fail      = (s_pair_state == PAIR_UI_FAIL);
    bool full      = (s_pair_state == PAIR_UI_FULL);
    bool mismatch  = (s_pair_state == PAIR_UI_MISMATCH);
    uint32_t tcol  = ok ? COL_GREEN : (fail || full || mismatch) ? COL_ALERT_NOLINK
                   : searching ? COL_LINK : COL_WHITE;
    lv_obj_set_style_text_color(T.pairTitle, lv_color_hex(tcol), 0);

    const char *btn1 = NULL;
    bool back = false;
    bool cta  = true;

    lv_obj_add_flag(T.offIcon, LV_OBJ_FLAG_HIDDEN);

    if (ok) {
        lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(T.pairTitle, "Paired");
        lv_obj_add_flag(T.pairInstr, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(T.pairHint, s_pair_state == PAIR_UI_SAVED
                                    ? LINK_OTA_SOAK_MSG : "Restarting");
        lv_obj_remove_flag(T.pairHint, LV_OBJ_FLAG_HIDDEN);
    } else if (fail) {
        lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
        pair_badge(&hero_alert, 32, COL_ALERT_NOLINK);
        lv_label_set_text(T.pairTitle, "Pairing failed");
        lv_label_set_text(T.pairInstr,
            "Check the Serin Controller is powered and in pairing mode.");
        lv_obj_remove_flag(T.pairInstr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.pairHint, LV_OBJ_FLAG_HIDDEN);
        btn1 = "Retry";
        back = s_link.bonded;
        cta  = false;
    } else if (full) {
        lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
        pair_badge(&hero_alert, 32, COL_ALERT_NOLINK);
        lv_label_set_text(T.pairTitle, "Zone limit reached");
        lv_obj_add_flag(T.pairInstr, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(T.pairHint, "Forget a zone to add another");
        lv_obj_remove_flag(T.pairHint, LV_OBJ_FLAG_HIDDEN);
        back = true;
    } else if (mismatch) {
        lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
        pair_badge(&set_link_row, 60, COL_ALERT_NOLINK);
        lv_label_set_text(T.pairTitle, "Controller reset");
        lv_label_set_text(T.pairInstr,
            "The Serin Controller was reset. Forget its zone, then pair again.");
        lv_obj_remove_flag(T.pairInstr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.pairHint, LV_OBJ_FLAG_HIDDEN);
        btn1 = zones_can_enter() ? "Zones" : NULL;
        back = true;
        cta  = false;
    } else if (searching) {
        lv_obj_add_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(T.pairTitle, "Pairing");
        lv_label_set_text(T.pairInstr,
            "Keep the controller's pairing window open "
            "until this finishes.");
        lv_obj_remove_flag(T.pairInstr, LV_OBJ_FLAG_HIDDEN);
        pair_hint_paint();
        lv_obj_remove_flag(T.pairHint, LV_OBJ_FLAG_HIDDEN);
        btn1 = "Cancel";
        cta  = false;
    } else {
        lv_obj_remove_flag(T.pairIcon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(T.pairTitle, "Ready to pair");
        lv_label_set_text(T.pairInstr,
            "Open pairing on the controller, then tap Pair.");
        lv_obj_remove_flag(T.pairInstr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.pairHint, LV_OBJ_FLAG_HIDDEN);
        btn1 = "Pair";
    }

    bool duo = (btn1 && back);
    lv_obj_set_style_pad_top(T.pairRow,
        SERIN_PX((fail || mismatch) ? PAIR_ROW_DY_FAIL
               : searching          ? PAIR_ROW_DY_SEARCH
                                    : PAIR_ROW_DY) + TF_ROW_PAD, 0);

    if (btn1) {
        lv_label_set_text(T.pairBtnLbl, btn1);
        pair_pill_geom(T.pairBtn, duo);
        lv_obj_set_style_bg_color(T.pairBtn,
            lv_color_hex(cta ? COL_HEAT_GLOW : COL_WHITE), 0);
        lv_obj_set_style_bg_opa(T.pairBtn, cta ? LV_OPA_COVER : OPA_FILL_NEUTRAL, 0);
        lv_obj_set_style_text_color(T.pairBtnLbl,
            lv_color_hex(cta ? COL_ON_ACCENT : COL_WHITE), 0);
        lv_obj_set_style_bg_color(T.pairBtn,
            lv_color_hex(cta ? COL_HEAT_BASE : COL_WHITE), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(T.pairBtn,
            cta ? LV_OPA_COVER : ACK_OPA_CHIP, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_remove_flag(T.pairBtn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(T.pairBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (back) {
        pair_pill_geom(T.pairBtn2, duo);
        lv_obj_remove_flag(T.pairBtn2, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(T.pairBtn2, LV_OBJ_FLAG_HIDDEN);
    }
    touch_floor_pair(T.pairBtn, &s_pairbtn_tf,
                     T.pairBtn2, &s_pairbtn2_tf, TF_CAP_PILL_ROW);

    if (searching) {
        s_arim_home.col = COL_LINK;
        arim_sweep_start(T.nolinkRing);
        lv_obj_remove_flag(T.nolinkRing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(T.nolinkRing);
    } else {
        arim_stop(T.nolinkRing);
        s_arim_home.col = ok ? COL_GREEN : (fail || full || mismatch) ? COL_ALERT_NOLINK : COL_HEAT_GLOW;
        lv_obj_remove_flag(T.nolinkRing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(T.nolinkRing);
    }
}

static void refresh_thermostat(void)
{
    const hvac_state_t *s = &s_state;
    uint32_t base, glow; const char *ml; mode_pal(s->mode, &base, &glow, &ml);
    bool off = (s->mode == MODE_OFF) || (s->status == ST_OFF);
    bool active = (s->status == ST_ACTIVE) && !off;
    uint32_t accent = off ? COL_GREY : glow;

    lv_obj_set_style_arc_color(T.arc, lv_color_hex(accent), LV_PART_INDICATOR);

    dial_face_t face = thermo_face();
    bool syncing = (s->alert == ALERT_SYNCING);
    bool tell = false;
    if (face != DIAL_LIVE) {
        s_sync_tell_ms = 0;
        if (s_sync_tell_timer) { lv_timer_delete(s_sync_tell_timer); s_sync_tell_timer = NULL; }
    } else if (syncing) {
        if (!s_sync_tell_ms) {
            s_sync_tell_ms = lv_tick_get();
            if (!s_sync_tell_ms) s_sync_tell_ms = 1;
        }
        tell = true;
    } else if (s_sync_tell_ms) {
        uint32_t el = lv_tick_elaps(s_sync_tell_ms);
        if (el < SYNC_TELL_MIN_MS) {
            tell = true;
            if (s_sync_tell_timer) lv_timer_delete(s_sync_tell_timer);
            s_sync_tell_timer = lv_timer_create(sync_tell_timer_cb, SYNC_TELL_MIN_MS - el, NULL);
            lv_timer_set_repeat_count(s_sync_tell_timer, 1);
        } else s_sync_tell_ms = 0;
    }
    bool remind     = face_dismissed(face);
    bool remindLink = remind && (face == DIAL_LINK_OFF);

    const face_mode_t mode = { face, face_is_blank(face), remind, s_glancing, syncing };
    static face_mode_t s_mode;
    static bool        s_mode_valid;
    const bool face_changed = !s_mode_valid || memcmp(&mode, &s_mode, sizeof mode) != 0;
    s_mode = mode;
    s_mode_valid = true;

    if (face_changed) {
        lv_obj_t *const owned[] = {
            T.num, T.numHalf, T.deg, T.roomDot, T.setDot, T.now, T.pill,
            T.alert, T.nolinkRing, T.offIcon, T.wifiHint,
            T.pairIcon, T.pairBox,
        };
        for (unsigned i = 0; i < sizeof owned / sizeof *owned; i++)
            if (owned[i]) lv_obj_add_flag(owned[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (s_glancing && face != DIAL_LIVE) s_glancing = false;

    if (face_is_blank(face)) {
        band_show(false);
        lv_obj_set_style_arc_opa(T.arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(T.arc, LV_OPA_TRANSP, LV_PART_MAIN);
        s_chip_is_batt = false;
        zchip_refresh();

        if (face == DIAL_UNPAIRED) { refresh_pairing_face(); return; }

        refresh_blank_offline(face);
        return;
    }

    if (syncing && !hvac_state_plausible(s)) { refresh_sync_skeleton(); return; }

    if (s_glancing) { refresh_glance(s); return; }

    if (!lock_pill_owns_slot())
        lv_obj_remove_flag(T.pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(T.pill, remind ? 1 : 0, 0);
    lv_obj_set_style_text_font(T.status, FONT_DIAL_MODE, 0);
    lv_obj_set_style_text_align(T.status, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(T.pill, LV_ALIGN_TOP_MID, 0, SERIN_PX(PILL_TOP));
    lv_obj_remove_flag(T.roomDot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(T.setDot,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(T.num, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(T.setDot, lv_color_white(), 0);
    lv_obj_set_style_arc_opa(T.arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(T.arc, remind ? REMIND_HERO_OPA : LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_remove_flag(T.now,     LV_OBJ_FLAG_HIDDEN);

    if (band_active()) {
        lv_obj_add_flag(T.num,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(T.deg,     LV_OBJ_FLAG_HIDDEN);
        band_show(true);
        lv_obj_set_style_bg_color(T.setDot,  lv_color_white(), 0);
        lv_obj_set_style_bg_color(T.setDot2, lv_color_white(), 0);
        lv_obj_remove_flag(T.setDot2, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(T.num, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(T.deg, LV_OBJ_FLAG_HIDDEN);
        band_show(false);
    }

    if (remind) {
        uint32_t rc = remindLink ? COL_ALERT_NOLINK : COL_ALERT_WARN;
        lv_obj_set_style_bg_opa(T.pill, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(T.status, lv_color_hex(rc), 0);
        lv_obj_set_style_border_color(T.pill, lv_color_hex(rc), 0);
        lv_label_set_text(T.status, "Offline");
    } else if (tell) {
        lv_obj_set_style_bg_color(T.pill, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(T.pill, LV_OPA_20, 0);
        lv_obj_set_style_text_color(T.status, lv_color_hex(COL_LINK), 0);
        lv_label_set_text(T.status, "Syncing");
    } else {
        lv_obj_set_style_bg_color(T.pill, lv_color_hex(accent), 0);
        lv_obj_set_style_bg_opa(T.pill, LV_OPA_30, 0);
        lv_obj_set_style_text_color(T.status, lv_color_hex(accent), 0);
        lv_label_set_text(T.status, status_text(s, active, off));
    }

    uint32_t numCol = (s_edit || s->alert == ALERT_PENDING) ? accent : COL_WHITE;
    if (remind) numCol = COL_WHITE;
    lv_obj_set_style_text_color(T.num,     lv_color_hex(numCol), 0);
    lv_obj_set_style_text_color(T.numHalf, lv_color_hex(numCol), 0);
    lv_opa_t heroOpa = remind ? REMIND_HERO_OPA : LV_OPA_COVER;
    lv_obj_set_style_text_opa(T.num,     heroOpa, 0);
    lv_obj_set_style_text_opa(T.numHalf, heroOpa, 0);
    lv_obj_set_style_text_opa(T.deg,     remind ? REMIND_HERO_OPA : HERO_DEG_OPA, 0);
    lv_color_t dotCol = remind
        ? lv_color_mix(lv_color_hex(COL_PURE), lv_color_black(), REMIND_HERO_OPA)
        : lv_color_hex(COL_PURE);
    lv_obj_set_style_bg_color(T.roomDot, dotCol, 0);
    lv_obj_set_style_bg_color(T.setDot,  dotCol, 0);
    lv_obj_set_style_bg_color(T.setDot2, dotCol, 0);

    lv_obj_t *nowlbl = lv_obj_get_user_data(T.now);
    lv_obj_set_style_text_opa(nowlbl, remind ? REMIND_HERO_OPA : NOW_TXT_OPA, 0);
    if (s->humidity_valid)
        lv_label_set_text_fmt(nowlbl, "Now %d\xC2\xB0 \xC2\xB7 %d%%", s->room_temp, s->humidity);
    else
        lv_label_set_text_fmt(nowlbl, "Now %d\xC2\xB0", s->room_temp);

    bool battSnoozed = batt_snoozed();
    bool showBatt = (s->alert == ALERT_NONE) && s->sensor_batt_low && !battSnoozed;

    if (!showBatt && (remind || s->alert == ALERT_NONE || tell)) {
        s_chip_is_batt = false;
        lv_obj_add_flag(T.alert, LV_OBJ_FLAG_HIDDEN);
    } else {
        const char *txt = "";
        s_chip_is_batt = false;
        if (showBatt) {
            txt = "LOW BATTERY"; s_chip_is_batt = true;
        } else switch (s->alert) {
            case ALERT_PENDING: txt = "CHANGE PENDING"; break;
            case ALERT_FAULT:   txt = "HEAT PUMP FAULT"; break;
            default: break;
        }
        lv_obj_set_style_border_color(T.alert, lv_color_hex(COL_ALERT_WARN), 0);
        lv_obj_set_style_image_recolor(T.alertIcon, lv_color_hex(COL_ALERT_WARN), 0);
        lv_obj_set_style_text_color(T.alertTxt, lv_color_hex(COL_ALERT_WARN), 0);
        lv_obj_set_style_text_font(T.alertTxt, showBatt ? FONT_CHIP_BATT : FONT_DIAL_CHIP, 0);
        lv_label_set_text(T.alertTxt, txt);
        if (showBatt) lv_obj_add_flag(T.alertIcon, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_remove_flag(T.alertIcon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(T.alert, LV_OBJ_FLAG_HIDDEN);
    }

    arim_stop(T.nolinkRing);

    zchip_refresh();

    s_dispSet10 = active_edit_dd();
    thermo_layout(s_dispSet10);
}

static void pair_hint_paint(void)
{
    if (s_pair_ch) lv_label_set_text_fmt(T.pairHint, "0:%02d", s_pair_left);
    else           lv_label_set_text_fmt(T.pairHint, "Searching \xC2\xB7 0:%02d", s_pair_left);
}

static void pair_cd_cb(lv_timer_t *t)
{
    (void)t;
    if (s_pair_left > 0) s_pair_left--;
    if (s_cur == SCREEN_THERMOSTAT && s_pair_state == PAIR_UI_SEARCHING) {
        pair_hint_paint();
    }
}

typedef enum { HOLD_NONE = -1, HOLD_LOCK_UNLOCK, HOLD_LINK_FORGET,
               HOLD_WIFI_CHANGE, HOLD_WIFI_SKIP, HOLD_ADV_RESET } hold_id_t;
static int       s_hold_ms;
static hold_id_t s_hold_target = HOLD_NONE;

static bool link_unpair_armed(void)
{
    return s_cur == SCREEN_SETTINGS && s_set_sel == SET_LINK &&
           s_link.bonded && s_pair_state == PAIR_UI_HIDDEN &&
           s_ota_st == OTA_UI_HIDDEN;
}

static bool wifi_change_ready(void)
{
    return s_link.online && s_state.wifi_setup_avail &&
           s_state.wifi_provisioned && s_state.wifi_connected &&
           (s_caps.features & SL2_FEAT_WIFI_SETUP) &&
           s_pair_state == PAIR_UI_HIDDEN && s_ota_st == OTA_UI_HIDDEN;
}

static bool wifi_change_armed(void)
{
    return s_cur == SCREEN_SETTINGS && s_set_sel == SET_WIFI && wifi_change_ready();
}

static bool wifi_skip_armed(void)
{
    return s_cur == SCREEN_THERMOSTAT && thermo_face() == DIAL_WIFI_OFF;
}

static void hold_link_complete(void)
{
    settings_set_link_text("Unpairing\xE2\x80\xA6");
    if (s_on_forget) s_on_forget();
}

static void wifi_change_complete(void)
{
    wifi_change_begin();
    settings_eat_wifi_click();
}

static void wifi_skip_complete(void)
{
    face_dismiss(DIAL_WIFI_OFF);
    uig_eat_click(s_dial_gest);
}

static bool lock_unlock_armed(void)   { return lock_gates(); }
static void lock_unlock_complete(void)
{
    child_lock_open_window(&s_child_lock);
    refresh_thermostat();
    uig_eat_click(s_dial_gest);
}

static bool adv_reset_armed(void)
{
    return s_cur == SCREEN_ADVANCED && advanced_on_reset_row();
}
static void adv_reset_complete(void) { reset_dialog_show(); }

typedef struct {
    bool      (*armed)(void);
    void      (*complete)(void);
    lv_obj_t *(*arc)(void);
    float     (*angle)(void);
    void      (*arm)(uint32_t col);
} hold_spec_t;

static lv_obj_t *thermo_hold_arc(void) { return T.holdArc; }

static float link_forget_angle(void) { return settings_row_angle(SET_LINK); }
static float wifi_change_angle(void) { return settings_row_angle(SET_WIFI); }

static void link_forget_arm(uint32_t col) { settings_hold_arm(SET_LINK, col); }
static void wifi_change_arm(uint32_t col) { settings_hold_arm(SET_WIFI, col); }

static const hold_spec_t HOLD_SPECS[] = {
    [HOLD_LOCK_UNLOCK] = { lock_unlock_armed, lock_unlock_complete, thermo_hold_arc,   NULL,               NULL },
    [HOLD_LINK_FORGET] = { link_unpair_armed, hold_link_complete,   settings_hold_arc, link_forget_angle,  link_forget_arm },
    [HOLD_WIFI_CHANGE] = { wifi_change_armed, wifi_change_complete, settings_hold_arc, wifi_change_angle,  wifi_change_arm },
    [HOLD_WIFI_SKIP]   = { wifi_skip_armed,   wifi_skip_complete,   thermo_hold_arc,   NULL,               NULL },
    [HOLD_ADV_RESET]   = { adv_reset_armed,   adv_reset_complete,   advanced_hold_arc, advanced_row_angle, NULL },
};

static lv_obj_t *hold_arc(void) { return HOLD_SPECS[s_hold_target].arc(); }

static void hold_cancel(void)
{
    if (s_hold_timer) { lv_timer_delete(s_hold_timer); s_hold_timer = NULL; }
    s_hold_ms = 0;
    if (s_hold_target != HOLD_NONE && hold_arc()) lv_obj_add_flag(hold_arc(), LV_OBJ_FLAG_HIDDEN);
    s_hold_target = HOLD_NONE;
    if (s_cur == SCREEN_SETTINGS) refresh_settings();
}

void ui_hold_abort(void) { hold_cancel(); }

static void hold_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_hold_target == HOLD_NONE) { hold_cancel(); return; }
    const hold_spec_t *spec = &HOLD_SPECS[s_hold_target];
    uint32_t col = (s_hold_target == HOLD_LINK_FORGET) ? COL_ALERT_NOLINK
                 : (s_hold_target == HOLD_WIFI_CHANGE) ? SET_COL[SET_WIFI]
                 : (s_hold_target == HOLD_ADV_RESET)   ? COL_ALERT_NOLINK
                                                       : COL_PURE;
    s_hold_ms += SERIN_HOLD_TICK_MS;
    if (s_hold_ms < SERIN_HOLD_GRACE_MS) return;
    if (!spec->arc()) { hold_cancel(); return; }

    if (s_hold_ms - SERIN_HOLD_TICK_MS < SERIN_HOLD_GRACE_MS) {
        if (spec->arm) spec->arm(col);
        lv_obj_remove_flag(spec->arc(), LV_OBJ_FLAG_HIDDEN);
    }

    int pct = (s_hold_ms - SERIN_HOLD_GRACE_MS) * 100 / (SERIN_HOLD_CONFIRM_MS - SERIN_HOLD_GRACE_MS);
    if (pct >= 100) {
        hold_id_t done = s_hold_target;
        if (s_hold_timer) { lv_timer_delete(s_hold_timer); s_hold_timer = NULL; }
        s_hold_ms = 0;
        lv_obj_add_flag(spec->arc(), LV_OBJ_FLAG_HIDDEN);
        s_hold_target = HOLD_NONE;
        HOLD_SPECS[done].complete();
        return;
    }
    if (spec->angle) {
        float a0 = spec->angle() - SERIN_IND_HALF_DEG;
        rim_set_span(spec->arc(), a0, a0 + 3.6f * pct);
    } else {
        lv_arc_set_bg_angles(spec->arc(), ARIM_ARC_A0,
                             ARIM_ARC_A0 + (ARIM_ARC_A1 - ARIM_ARC_A0) * pct / 100);
    }
    lv_obj_set_style_arc_color(spec->arc(), lv_color_hex(col), LV_PART_MAIN);
}

void ui_button_down(void)
{
    s_btn_down = true;
    if (dlg_is_open()) return;
    if (dlg_resetting()) return;
    note_activity();
    s_hold_target = HOLD_NONE;
    for (int i = 0; i < (int)(sizeof HOLD_SPECS / sizeof *HOLD_SPECS); i++)
        if (HOLD_SPECS[i].armed()) { s_hold_target = (hold_id_t)i; break; }
    if (s_hold_target == HOLD_NONE) {
        /* A second press (touch while the knob is held) found nothing armed:
         * stop the first press's timer rather than let it tick with no spec. */
        if (s_hold_timer) hold_cancel();
        return;
    }
    s_hold_ms = 0;
    if (!s_hold_timer) s_hold_timer = lv_timer_create(hold_timer_cb, SERIN_HOLD_TICK_MS, NULL);
}

void ui_button_up(void)
{
    s_btn_down = false;
    if (s_hold_timer) hold_cancel();
}

screen_id_t ui_current_screen(void) { return s_cur; }

void ui_set_pairing(pair_ui_t state, uint8_t channel)
{
    if (state != PAIR_UI_HIDDEN) alert_dialog_hide();
    if (channel) s_pair_ch = channel;

    if (state == PAIR_UI_PROMPT) {
        s_pair_state = PAIR_UI_HIDDEN;
        if (s_pair_cd) { lv_timer_delete(s_pair_cd); s_pair_cd = NULL; }
        if (s_link.bonded) { pair_dismiss(); return; }
        ui_show_screen(SCREEN_THERMOSTAT);
        return;
    }
    if (state == PAIR_UI_HIDDEN) {
        s_pair_state = PAIR_UI_HIDDEN;
        if (s_pair_cd) { lv_timer_delete(s_pair_cd); s_pair_cd = NULL; }
        ui_show_screen(SCREEN_THERMOSTAT);
        return;
    }

    bool was_searching = (s_pair_state == PAIR_UI_SEARCHING);
    if (state == PAIR_UI_SEARCHING && !was_searching) {
        s_pair_left = 45;
        s_pair_ch = channel;
        if (!s_pair_cd) s_pair_cd = lv_timer_create(pair_cd_cb, 1000, NULL);
    }
    if (state != PAIR_UI_SEARCHING && s_pair_cd) { lv_timer_delete(s_pair_cd); s_pair_cd = NULL; }

    bool same_state = (state == s_pair_state);
    s_pair_state = state;

    if (state == PAIR_UI_SEARCHING && same_state) {
        if (s_cur == SCREEN_THERMOSTAT) {
            pair_hint_paint();
        }
        return;
    }
    ui_show_screen(SCREEN_THERMOSTAT);
}

void ui_set_ota(ota_ui_t st, int pct, const char *txt)
{
    s_ota_st = st; s_ota_pct = pct;
    if (txt) snprintf(s_ota_txt, sizeof(s_ota_txt), "%s", txt);
    note_activity();
    alert_dialog_hide();
    if (st == OTA_UI_HIDDEN) {
        if (s_cur == SCREEN_UPDATE) {
            settings_land(SET_ABOUT);
        }
        return;
    }
    if (s_cur != SCREEN_UPDATE) ui_show_screen(SCREEN_UPDATE);
    else refresh_update();
}

typedef struct {
    void (*turn)(int delta);
    void (*press)(void);
    void (*hold)(void);
    bool (*can_enter)(void);
    void (*build)(void);
    void (*destroy)(void);
    void (*enter)(void);
    void (*refresh)(void);
    void (*opened)(void);
    void (*caps_changed)(void);
    bool lazy;
    /* A wizard or a progress readout: reachable, but never somewhere Back
     * should land, because the run that put it on screen is over. */
    bool transient;
} screen_ops_t;

/* Until the controller confirms the zone, a change would be dropped. Refuse
 * it on the home face, where the Syncing chip says why. */
bool sync_refuse(void)
{
    if (s_state.alert != ALERT_SYNCING) return false;
    ui_show_screen(SCREEN_THERMOSTAT);
    return true;
}

static void thermo_turn(int delta)
{
    if (coach_demo_turn(delta)) return;
    if (thermo_face() != DIAL_LIVE) return;
    if (lock_refuse()) return;
    if (s_state.alert == ALERT_SYNCING)
        return;
    set_setpoint(*edit_slot() + delta * dial_step_dd(), true);
    notify_edit();
}

static void thermo_press(void)
{
    if (lock_refuse()) return;
    dial_face_t face = thermo_face();
    if (face == DIAL_UNPAIRED) {
        pair_face_press();
    } else if (face == DIAL_WIFI_OFF) {
        wifi_setup_entry();
    } else if ((face == DIAL_LINK_OFF || face == DIAL_CN105_OFF) &&
               !face_dismissed(face)) {
        offline_dialog_show(face);
    } else {
        ui_show_screen(SCREEN_MENU);
    }
}

static void thermo_hold(void)
{
    if (lock_gates()) return;
    if (!swipe_gate_open()) return;
    ui_show_screen(SCREEN_ZONES);
}

static const screen_ops_t SCREEN_OPS[SCREEN_COUNT] = {
    [SCREEN_THERMOSTAT] = { .turn = thermo_turn,   .press = thermo_press,   .hold = thermo_hold,
                            .build = build_thermostat, .refresh = refresh_thermostat },
    [SCREEN_FANVANE]    = { .turn = fan_turn,      .press = fanvane_press,
                            .build = build_fanvane,
                            .enter = fanvane_enter, .refresh = refresh_fanvane ,
                            .caps_changed = fanvane_caps_changed },
    [SCREEN_MENU]       = { .turn = menu_turn,     .press = menu_press,
                            .build = build_menu,   .refresh = refresh_menu ,
                            .caps_changed = menu_caps_changed },
    [SCREEN_MODE]       = { .turn = mode_turn,     .press = mode_commit,
                            .build = build_mode,   .destroy = mode_destroy,
                            .enter = mode_enter,   .refresh = refresh_mode ,
                            .caps_changed = mode_caps_changed, .lazy = true },
    [SCREEN_SETTINGS]   = { .turn = settings_turn, .press = settings_press,
                            .build = build_settings,
                            .enter = settings_enter, .refresh = refresh_settings ,
                            .caps_changed = settings_caps_changed },
    [SCREEN_UPDATE]     = { .turn = update_turn,   .press = update_press, .hold = update_hold,
                            .build = build_update, .refresh = refresh_update,
                            .transient = true },
    [SCREEN_ABOUT]      = { .turn = about_turn,    .press = about_press,
                            .build = build_about,  .destroy = about_destroy,
                            .enter = about_enter,  .refresh = refresh_about ,
                            .caps_changed = about_caps_changed, .lazy = true },
    [SCREEN_WIFI]       = { .turn = wifi_encoder,  .press = wifi_press,     .hold = wifi_back,
                            .build = build_wifi,   .destroy = wifi_destroy,
                            .refresh = refresh_wifi, .caps_changed = refresh_wifi,
                            .lazy = true, .transient = true },
    [SCREEN_ZONES]      = { .turn = zones_turn,    .press = zones_commit,
                            .can_enter = zones_can_enter,
                            .build = build_zones,
                            .refresh = refresh_zones, .opened = zones_opened,
                            .lazy = true },
    [SCREEN_ROOM_SOURCE] = { .turn = room_source_turn, .press = room_source_press,
                             .can_enter = room_source_can_enter,
                             .build = build_room_source, .destroy = room_source_destroy,
                             .enter = room_source_enter, .refresh = refresh_room_source,
                             .lazy = true },
    [SCREEN_ADVANCED]   = { .turn = advanced_turn, .press = advanced_press,
                            .build = build_advanced, .destroy = advanced_destroy,
                            .refresh = refresh_advanced, .lazy = true },
};

static void refresh_cur_screen(void)
{
    if (s_disp_asleep) { s_refresh_pending = true; return; }
    if (SCREEN_OPS[s_cur].refresh) SCREEN_OPS[s_cur].refresh();
}

static lv_obj_t   *s_leaver;
static void leaver_settle(void);

void screen_rebuild(screen_id_t id)
{
    const screen_ops_t *op = &SCREEN_OPS[id];
    /* A fade may still own this panel as its leaver; settle it first so the
     * pending dip/fade callback never touches the object deleted below. */
    if (s_leaver && s_leaver == s_panel[id]) leaver_settle();
    if (s_panel[id]) {
        if (op->destroy) op->destroy();
        lv_obj_delete(s_panel[id]);
        s_panel[id] = NULL;
    }
    if (s_cur != id && op->lazy) return;
    if (op->build) op->build();
    if (s_cur == id) {
        if (op->refresh) op->refresh();
    } else if (s_panel[id]) {
        lv_obj_add_flag(s_panel[id], LV_OBJ_FLAG_HIDDEN);
    }
}

static screen_id_t s_leaver_id;
static screen_id_t s_arriver_id;

static void leaver_settle(void)
{
    lv_obj_t *p = s_leaver;
    if (!p) return;
    s_leaver = NULL;
    lv_anim_delete(p, NULL);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(p, LV_OPA_COVER, 0);
    if (SCREEN_OPS[s_leaver_id].lazy && s_leaver_id != s_cur) {
        if (SCREEN_OPS[s_leaver_id].destroy) SCREEN_OPS[s_leaver_id].destroy();
        lv_obj_delete(p); s_panel[s_leaver_id] = NULL;
    }
    if (s_arriver_id == s_cur && s_panel[s_cur]) {
        lv_obj_remove_flag(s_panel[s_cur], LV_OBJ_FLAG_HIDDEN);
        if (!s_on_bright_scale) ui_fade_in(s_panel[s_cur], UI_FADE_MS);
    }
}
static void leaver_done_cb(lv_anim_t *a) { (void)a; leaver_settle(); }

/* Where Back goes.  The thermostat is the root, so arriving there empties the
 * trail; arriving anywhere already on it rewinds to that point instead of
 * stacking a second copy.  Everything else is one step deeper than its
 * opener, which is what lets Mode and Airflow return to the Menu when the
 * Menu is how the user got there and to Home when it isn't. */
#define BACK_MAX 6
static screen_id_t s_back[BACK_MAX];
static int         s_back_n = 0;

static void back_note(screen_id_t id, screen_id_t prev)
{
    if (id == SCREEN_THERMOSTAT) { s_back_n = 0; return; }
    for (int i = 0; i < s_back_n; i++)
        if (s_back[i] == id) { s_back_n = i; return; }
    if (id == prev || SCREEN_OPS[prev].transient) return;
    if (s_back_n == BACK_MAX) {
        memmove(s_back, s_back + 1, sizeof s_back - sizeof s_back[0]);
        s_back_n--;
    }
    s_back[s_back_n++] = prev;
}

/* A screen can lose the capability that made it reachable while it sits on the
 * trail, so skip anything that would now refuse to open.  ui_show_screen
 * rewinds the trail to whatever this picks, which drops the skipped entries. */
screen_id_t ui_back_target(void)
{
    for (int i = s_back_n - 1; i >= 0; i--) {
        const screen_ops_t *op = &SCREEN_OPS[s_back[i]];
        if (!op->can_enter || op->can_enter()) return s_back[i];
    }
    return SCREEN_THERMOSTAT;
}

void ui_back(void) { ui_show_screen(ui_back_target()); }

void ui_show_screen(screen_id_t id)
{
    if (id >= SCREEN_COUNT) return;
    const screen_ops_t *op = &SCREEN_OPS[id];

    if (op->can_enter && !op->can_enter()) return;
    back_note(id, s_cur);
    leaver_settle();
    if (id != s_cur && s_hold_timer) hold_cancel();
    if (op->build && !s_panel[id]) op->build();

    screen_id_t prev = s_cur;
    bool fade = prev != id && s_anim_en && s_panel[prev] &&
                !lv_obj_has_flag(s_panel[prev], LV_OBJ_FLAG_HIDDEN);
    if (s_panel[prev] && !fade) {
        lv_obj_add_flag(s_panel[prev], LV_OBJ_FLAG_HIDDEN);
        lv_anim_delete(s_panel[prev], NULL);
        lv_obj_set_style_opa(s_panel[prev], LV_OPA_COVER, 0);
    }
    s_cur = id;

    if (prev == SCREEN_THERMOSTAT && id == SCREEN_MENU) menu_select_home();
    if (op->enter)   op->enter();
    if (op->refresh) op->refresh();
    lv_obj_set_style_opa(s_panel[id], LV_OPA_COVER, 0);
    if (fade) {
        /* A lazy screen was just built visible, on top of the leaver. */
        lv_obj_add_flag(s_panel[id], LV_OBJ_FLAG_HIDDEN);
        s_leaver = s_panel[prev]; s_leaver_id = prev; s_arriver_id = id;
        if (s_on_bright_scale) { if (!dip_swap(leaver_settle, s_dip_out_ms)) leaver_settle(); }
        else ui_fade_out(s_leaver, UI_FADE_OUT_MS, leaver_done_cb);
    } else {
        lv_obj_remove_flag(s_panel[id], LV_OBJ_FLAG_HIDDEN);
        if (prev != id && SCREEN_OPS[prev].lazy) {
            if (SCREEN_OPS[prev].destroy) SCREEN_OPS[prev].destroy();
            if (s_panel[prev]) { lv_obj_delete(s_panel[prev]); s_panel[prev] = NULL; }
        }
    }
    if (op->opened) op->opened();
}

void ui_encoder(int delta)
{
    note_activity();
    if (dlg_resetting()) return;
    if (s_glancing)    { glance_exit(); return; }
    if (s_disp_asleep) { disp_wake();   return; }
    if (coach_demo_turn(delta)) return;
    if (dlg_is_open()) { dlg_turn(delta); return; }
    if (SCREEN_OPS[s_cur].turn) SCREEN_OPS[s_cur].turn(delta);
}

void ui_button(void)
{
    note_activity();
    if (dlg_resetting()) return;
    if (s_glancing)    { glance_exit(); return; }
    if (s_disp_asleep) { disp_wake();   return; }
    if (dlg_is_open()) { dlg_press(); return; }
    if (SCREEN_OPS[s_cur].press) SCREEN_OPS[s_cur].press();
}

static bool confirm_hold_armed(void)
{
    return lock_unlock_armed() || link_unpair_armed() || wifi_change_armed() ||
           wifi_skip_armed() || adv_reset_armed();
}

void ui_button_long(void)
{
    note_activity();
#if CONFIG_SERIN_THERMAL_CAL
    if (s_cal_owned) { thermal_cal_dump(); return; }
#endif
    if (dlg_resetting()) return;
    if (s_glancing)    { glance_exit(); return; }
    if (s_disp_asleep) { disp_wake();   return; }
    if (dlg_is_open()) { dlg_close_by_user(1); return; }
    if (confirm_hold_armed()) return;
    if (s_pair_state != PAIR_UI_HIDDEN) {
        if (s_pair_state == PAIR_UI_SEARCHING) pair_cancel_request();
        return;
    }
    if (SCREEN_OPS[s_cur].hold) SCREEN_OPS[s_cur].hold();
    else                        ui_show_screen(SCREEN_THERMOSTAT);
}

/* A plain tap on the home face: the outer SERIN_SIDE_TAP_PCT of the width on
 * either side steps the setpoint (left down, right up) exactly as one knob
 * click would; anything else falls through to the knob press (Menu). */
static bool home_side_tap(void)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return false;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    int edge = SERIN_DISP * SERIN_SIDE_TAP_PCT / 100;
    int dir  = pt.x < edge ? -1 : pt.x >= SERIN_DISP - edge ? +1 : 0;
    if (!dir) return false;
    note_activity();
    if (!coach_demo_tap(dir)) thermo_turn(dir);
    return true;
}

static void panel_click_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;

    if (s_dial_gest && lv_event_get_target(e) == s_panel[SCREEN_THERMOSTAT]) {
        if (!uig_click_is_tap(s_dial_gest)) return;
        if (dlg_is_open() || s_glancing || s_disp_asleep) { ui_button(); return; }
        if (home_side_tap()) return;
    }
    ui_button();
}

static void pill_click_cb(lv_event_t *e)
{
    (void)e;
    if (dial_gesture_tail()) return;
    if (lock_refuse()) return;
    switch (thermo_face()) {
        case DIAL_UNPAIRED:  pair_face_press();                 break;
        case DIAL_WIFI_OFF:  wifi_setup_entry();                break;
        case DIAL_LINK_OFF:  offline_dialog_show(DIAL_LINK_OFF);  break;
        case DIAL_CN105_OFF: offline_dialog_show(DIAL_CN105_OFF); break;
        default:
            if (s_state.alert == ALERT_SYNCING && !hvac_state_plausible(&s_state))
                break;
            ui_show_screen(SCREEN_MODE);
            break;
    }
}

static void alert_click_cb(lv_event_t *e)
{
    (void)e;
    if (dial_gesture_tail()) return;
    if (s_chip_is_batt) {
        batt_dialog_show();
        return;
    }
    if (s_state.alert != ALERT_PENDING && s_state.alert != ALERT_FAULT) return;
    if (s_state.alert == ALERT_PENDING) {
        alert_dialog_show(COL_ALERT_WARN, "Change pending",
            "Your change was sent to the heat pump and is waiting to be "
            "confirmed. This usually takes a few seconds.",
            "Open Settings", DLG_GOTO_HEATPUMP);
    } else {
        fault_dialog_show("Open Settings");
    }
}

static void pair_retry(void)
{
    ui_set_pairing(PAIR_UI_SEARCHING, 0);
    s_on_pair();
}

static void pair_dismiss(void)
{
    s_pair_state = PAIR_UI_HIDDEN;
    settings_land(SET_LINK);
}

void begin_pairing(void)
{
    if (s_pair_state == PAIR_UI_HIDDEN ||
        (!s_link.bonded && s_pair_state == PAIR_UI_FAIL)) pair_retry();
    else                                                  pair_dismiss();
}

static void pair_face_press(void)
{
    if (s_pair_state == PAIR_UI_SEARCHING) {
        pair_cancel_request();
    } else {
        begin_pairing();
    }
}

static void pair_btn_cb(lv_event_t *e)
{
    (void)e;
    if (dial_gesture_tail()) return;
    if (s_pair_state == PAIR_UI_SEARCHING) {
        pair_cancel_request();
    } else if (s_pair_state == PAIR_UI_MISMATCH) {
        s_pair_state = PAIR_UI_HIDDEN;
        ui_show_screen(SCREEN_ZONES);
    } else {
        pair_retry();
    }
}

static void pair_btn2_cb(lv_event_t *e)
{
    (void)e;
    if (dial_gesture_tail()) return;
    pair_dismiss();
}

void ui_set_state(const hvac_state_t *s)
{
#if CONFIG_SERIN_THERMAL_CAL
    if (s_cal_owned && !s_cal_writing) return;
#endif
    if (s) {
        fan_adopt(s->fan_pct);
        s_state = *s;
        if (!ui_adv_editing_units())
            s_units_c = !s->use_f;
        if (s->alert != ALERT_SYNCING) {
            int z = s_zactive;
            if (s->alert != ALERT_NOLINK) s_link_dismissed [z] = false;
            if (s->cn105_connected)       s_cn105_dismissed[z] = false;
            if (!s->sensor_batt_low || !s_batt_was_low[z])
                s_batt_snooze_to[z] = 0;
            s_batt_was_low[z] = s->sensor_batt_low;
        }
    }
    dlg_check_close();
    refresh_cur_screen();
}
void ui_get_state(hvac_state_t *out) { if (out) *out = s_state; }
int  ui_band_edge(void) { return band_sel(); }
int  ui_active_zone(void) { return s_zactive; }
int  ui_zone_count(void)  { return s_zn_ui; }
int  ui_glancing(void)    { return s_glancing; }
int  ui_disp_asleep(void) { return s_disp_asleep; }
const char *ui_dial_face(void)
{
    switch (thermo_face()) {
        case DIAL_LIVE:      return "live";
        case DIAL_UNPAIRED:  return "unpaired";
        case DIAL_LINK_OFF:  return "link_off";
        case DIAL_CN105_OFF: return "cn105_off";
        case DIAL_WIFI_OFF:  return "wifi_off";
    }
    return "?";
}

int  ui_hold_arc_shown(void) {
    lv_obj_t *sa = settings_hold_arc();
    lv_obj_t *aa = advanced_hold_arc();
    return (sa && !lv_obj_has_flag(sa, LV_OBJ_FLAG_HIDDEN)) ||
           (aa && !lv_obj_has_flag(aa, LV_OBJ_FLAG_HIDDEN)) ||
           (T.holdArc && !lv_obj_has_flag(T.holdArc, LV_OBJ_FLAG_HIDDEN));
}

int  ui_settings_sel(void) { return s_set_sel; }
bool ui_units_c(void)      { return s_units_c; }

int  ui_child_lock_enabled(void)     { return s_child_lock.enabled ? 1 : 0; }
int  ui_child_lock_window_open(void) { return s_child_lock.window_open ? 1 : 0; }

void ui_idle_off_set(bool off)
{
    s_idle_off = off;
    prefs_set_u8("idle_off", off ? 1 : 0);
    sensor_sht40_set_idle_off(off);
}
bool ui_idle_off_get(void) { return s_idle_off; }

void ui_night_en_set(bool on)
{
    s_night_en = on;
    prefs_set_u8("night_en", on ? 1 : 0);
}
bool ui_night_en_get(void) { return s_night_en; }

void ui_anim_en_set(bool on)
{
    s_anim_en = on;
    prefs_set_u8("anim_en", on ? 1 : 0);
}
bool ui_anim_en_get(void) { return s_anim_en; }

void ui_init(void)
{
    s_root = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, SERIN_DISP, SERIN_DISP);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(s_root);

    ui_state_defaults(&s_state);
    s_wifi_dismissed[0] = prefs_get_bool("wifi_skip", false);
    s_idle_off = prefs_get_u8("idle_off", 0) != 0;
    s_night_en = prefs_get_u8("night_en", 1) != 0;
    s_anim_en  = prefs_get_u8("anim_en", 1) != 0;
    sensor_sht40_set_idle_off(s_idle_off);
    child_lock_init(&s_child_lock, prefs_get_u8("lk_en", 0) != 0);
    for (int i = 0; i < SCREEN_COUNT; i++)
        if (!SCREEN_OPS[i].lazy && SCREEN_OPS[i].build) SCREEN_OPS[i].build();

    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (SCREEN_OPS[i].refresh) SCREEN_OPS[i].refresh();
        if (i != s_cur && s_panel[i]) lv_obj_add_flag(s_panel[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_idle_timer = lv_timer_create(idle_timer_cb, IDLE_POLL_MS, NULL);
    lv_display_add_event_cb(lv_display_get_default(), render_ready_cb,
                            LV_EVENT_RENDER_READY, NULL);
    lv_timer_create(lvgl_wdt_cb, LVGL_WDT_FEED_MS, NULL);

    splash_show();
}

static void ui_state_defaults(hvac_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->mode = MODE_HEAT;  s->status = ST_ACTIVE;  s->alert = ALERT_NONE;
    s->use_f = true;
    s->room_temp = 69;    s->set_dd = 720;
    s->set_lo_dd = 680;   s->set_hi_dd = 740;
    s->band_valid = true;
    s->humidity = 44;     s->humidity_valid = true;
    s->fan_pct = 60;      s->vvane = VANE_2;       s->hvane = HVANE_CENTER;
    s->sub_mode = "";
    s->poll_ms = 2000;
    s->sensor_batt_low = false;  s->sensor_batt_pct = -1;
}

static bool caps_range_usable(const ui_caps_t *c)
{
    return caps_ddmax(c, true)  - caps_ddmin(c, true)  >= caps_step_dd(c, true) &&
           caps_ddmax(c, false) - caps_ddmin(c, false) >= caps_step_dd(c, false);
}

void ui_set_caps(const ui_caps_t *caps)
{
    ui_caps_t c = *caps;
    if (c.modes == 0) c.modes = 0x007F;
    if (!caps_range_usable(&c)) { c.set_min_dc = 160; c.set_max_dc = 305; }
    s_caps = c;
    if (s_state.set_dd < dial_ddmin()) s_state.set_dd = dial_ddmin();
    if (s_state.set_dd > dial_ddmax()) s_state.set_dd = dial_ddmax();

    for (int i = 0; i < SCREEN_COUNT; i++)
        if (SCREEN_OPS[i].caps_changed) SCREEN_OPS[i].caps_changed();
}

void ui_set_link_sensor(bool present, bool valid, int16_t temp_dc, uint8_t hum_pct)
{
    if (s_state.link_sens_present == present && s_state.link_sens_valid == valid &&
        s_state.link_temp_dc == temp_dc && s_state.link_hum_pct == hum_pct) return;
    s_state.link_sens_present = present;
    s_state.link_sens_valid   = valid;
    s_state.link_temp_dc      = temp_dc;
    s_state.link_hum_pct      = hum_pct;
    ui_set_state(&s_state);
}

void ui_set_room_src(uint8_t src, uint8_t status)
{
    if (s_state.room_src == src && s_state.room_src_status == status) return;
    s_state.room_src        = src;
    s_state.room_src_status = status;
    ui_set_state(&s_state);
}

void ui_set_room_src_name(const char *name)
{
    const char *n = name ? name : "";
    if (strncmp(s_state.room_src_name, n, sizeof s_state.room_src_name) == 0) return;
    snprintf(s_state.room_src_name, sizeof s_state.room_src_name, "%s", n);
    ui_set_state(&s_state);
}

#if CONFIG_SERIN_THERMAL_CAL

void ui_cal_take(bool take)
{
    s_cal_owned = take;
    if (take) {
        s_glancing = false;
        ui_show_screen(SCREEN_THERMOSTAT);
    }
}

void ui_cal_face(ui_cal_face_t f)
{
    hvac_state_t st = {
        .mode             = MODE_HEAT,
        .status           = ST_IDLE,
        .room_temp        = 70,
        .set_dd           = 700,
        .use_f            = true,
        .alert            = ALERT_NONE,
        .cn105_connected  = true,
        .wifi_provisioned = true,
        .wifi_setup_avail = false,
        .humidity_valid   = false,
        .band_valid       = false,
    };
    bool bonded = true;

    switch (f) {
    case UI_CAL_CN105_OFF: st.cn105_connected = false; break;
    case UI_CAL_UNPAIRED:  bonded = false;             break;
    case UI_CAL_LIVE:
    case UI_CAL_GLANCE:    break;
    }

    s_cal_writing = true;
    ui_set_link_state(bonded, bonded, NULL, -50);
    ui_set_state(&st);
    s_cal_writing = false;

    s_glancing = (f == UI_CAL_GLANCE);
    refresh_thermostat();
}
#endif

void zchip_refresh(void)
{
    if (!T.zchip) return;
    dial_face_t f = thermo_face();
    bool faceOk = !face_is_blank(f) ||
                  f == DIAL_LINK_OFF || f == DIAL_CN105_OFF;
    bool show = faceOk && (s_zn_ui >= 2) && s_link.bonded && !s_glancing &&
                lv_obj_has_flag(T.alert, LV_OBJ_FLAG_HIDDEN);
    if (!show) {
        lv_obj_add_flag(T.zchip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_align(T.zchip, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(CHIP_BOT_Y));
        char nm[48]; zone_disp_name(s_zactive, nm, sizeof nm);
        lv_label_set_text(T.zname, nm);
        lv_obj_remove_flag(T.zchip, LV_OBJ_FLAG_HIDDEN);
    }

    airbtn_refresh();
}

#define ZCHIP_PULSE_MS   150
#define ZCHIP_PULSE_OPA  110
void zchip_pulse(void)
{
    if (!T.zchip || lv_obj_has_flag(T.zchip, LV_OBJ_FLAG_HIDDEN)) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, T.zchip);
    lv_anim_set_exec_cb(&a, anim_bg_opa_exec);
    lv_anim_set_values(&a, ZCHIP_IDLE_OPA, ZCHIP_PULSE_OPA);
    lv_anim_set_duration(&a, ZCHIP_PULSE_MS);
    lv_anim_set_playback_duration(&a, ZCHIP_PULSE_MS);
    lv_anim_start(&a);
}

static void airbtn_refresh(void)
{
    if (!T.airbtn) return;
    bool show = s_zn_ui < 2 && !s_glancing && swipe_v_gate_open() &&
                lv_obj_has_flag(T.alert, LV_OBJ_FLAG_HIDDEN);
    if (show) lv_obj_remove_flag(T.airbtn, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(T.airbtn, LV_OBJ_FLAG_HIDDEN);
}

static void airbtn_click_cb(lv_event_t *e)
{
    (void)e;
    if (dial_gesture_tail()) return;
    airbtn_refresh();
    if (lv_obj_has_flag(T.airbtn, LV_OBJ_FLAG_HIDDEN)) return;
    if (lock_refuse()) return;
    ui_show_screen(SCREEN_FANVANE);
}

static void zchip_click_cb(lv_event_t *e)
{
    (void)e;
    if (dial_gesture_tail()) return;
    if (lock_refuse()) return;
    ui_show_screen(SCREEN_ZONES);
}

void ui_set_zones(const ui_zone_info_t *z, int n, int active)
{
    if (n > UI_MAX_ZONES) n = UI_MAX_ZONES;
    if (n < 0) n = 0;
    for (int i = 0; i < s_zn_ui; i++) {
        bool present = false;
        for (int j = 0; j < n && !present; j++)
            present = memcmp(s_zinfo[i].mac, z[j].mac, 6) == 0;
        char kb[16];
        const char *k = wifi_skip_key(kb, s_zinfo[i].mac);
        if (!present && prefs_get_bool(k, false)) prefs_set_bool(k, false);
    }
    for (int i = 0; i < UI_MAX_ZONES; i++) {
        bool same = (i < n) && (i < s_zn_ui) &&
                    memcmp(s_zinfo[i].mac, z[i].mac, 6) == 0;
        if (!same) {
            s_batt_was_low[i]    = false;
            s_batt_snooze_to[i]  = 0;
            s_link_dismissed[i]  = false;
            s_cn105_dismissed[i] = false;
            char kb[16];
            s_wifi_dismissed[i] = (i < n) &&
                prefs_get_bool(wifi_skip_key(kb, z[i].mac), false);
        }
    }
    if (n) memcpy(s_zinfo, z, (size_t)n * sizeof *z);
    s_zn_ui   = n;
    s_zactive = (active >= 0 && active < n) ? active : 0;
    zones_changed(n);

    menu_recheck();

    refresh_cur_screen();
}
void ui_set_on_zone_select(void (*cb)(int)) { s_on_zone_select = cb; }
void ui_set_on_zones_open(void (*cb)(void)) { s_on_zones_open = cb; }
void notify_zone_select(int i)
{
    if (s_on_zone_select) s_on_zone_select(i);
    else refresh_thermostat();
}
void notify_zones_open(void) { if (s_on_zones_open) s_on_zones_open(); }

void home_hide_alert_chip(void) { if (T.alert) lv_obj_add_flag(T.alert, LV_OBJ_FLAG_HIDDEN); }
void home_stop_alert_rim(void)  { if (T.nolinkRing) arim_stop(T.nolinkRing); }

bool home_pill_rect(lv_area_t *out)
{
    if (!T.pill || lv_obj_has_flag(T.pill, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_obj_update_layout(T.pill);
    lv_obj_get_coords(T.pill, out);
    return true;
}

bool home_hero_rect(lv_area_t *out)
{
    if (band_active()) {
        if (!T.bandLo || lv_obj_has_flag(T.bandLo, LV_OBJ_FLAG_HIDDEN)) return false;
        lv_obj_update_layout(T.bandLo);
        lv_area_t a, b, r;
        lv_obj_get_coords(T.bandLo, &a);
        lv_obj_get_coords(T.bandHi, &b);
        lv_obj_get_coords(T.bandRing, &r);
        out->x1 = LV_MIN(LV_MIN(a.x1, b.x1), r.x1);
        out->y1 = LV_MIN(LV_MIN(a.y1, b.y1), r.y1);
        out->x2 = LV_MAX(LV_MAX(a.x2, b.x2), r.x2);
        out->y2 = LV_MAX(LV_MAX(a.y2, b.y2), r.y2);
        return true;
    }
    if (!T.num || lv_obj_has_flag(T.num, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_obj_update_layout(T.num);
    lv_area_t n, d;
    lv_obj_get_coords(T.num, &n);
    lv_obj_get_coords(T.deg, &d);
    out->x1 = LV_MIN(n.x1, d.x1);
    out->y1 = LV_MIN(n.y1, d.y1);
    out->x2 = LV_MAX(n.x2, d.x2);
    out->y2 = LV_MAX(n.y2, d.y2);
    if (T.numHalf && !lv_obj_has_flag(T.numHalf, LV_OBJ_FLAG_HIDDEN)) {
        lv_area_t h; lv_obj_get_coords(T.numHalf, &h);
        if (h.x2 > out->x2) out->x2 = h.x2;
        if (h.y2 > out->y2) out->y2 = h.y2;
    }
    return true;
}

bool home_airbtn_rect(lv_area_t *out)
{
    if (!T.airbtn || lv_obj_has_flag(T.airbtn, LV_OBJ_FLAG_HIDDEN)) return false;
    lv_obj_update_layout(T.airbtn);
    lv_obj_get_coords(T.airbtn, out);
    return true;
}

int32_t home_arc_fill_deg(void)
{
    float t = serin_temp_t((float)s_state.set_dd, dial_ddmin(), dial_ddmax());
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (int32_t)(t * ARIM_SPAN_DEG + 0.5f);
}
