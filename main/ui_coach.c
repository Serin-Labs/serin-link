/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_arim.h"
#include "prefs.h"

#define COACH_KEY            "coach"
#define COACH_POLL_MS        1000
#define COACH_PRESENT_MS     10000
#define COACH_BOOT_GRACE_MS  30000
#define COACH_WD_MS          200
#define COACH_STEP_LIFE_MS   8000
#define COACH_TURN_SETTLE_MS 1500
#define COACH_FADE_IN_MS     200
#define COACH_FADE_OUT_MS    150
#define COACH_XFADE_OUT_MS   120
#define COACH_XFADE_IN_MS    150
#define COACH_SCRIM_OPA      LV_OPA_90
#define COACH_HOLE_PAD       8
#define COACH_CAP_GAP        10
#define COACH_CAP_Y_LOW      352
#define COACH_RING_STEP_DEG  9
#define ARROW_PX             48
#define ARROW_Y0             384
#define ARROW_RISE_PX        110
#define ARROW_RISE_MS        600
#define ARROW_LOOP_MS        1400
#define TARR_BOX         128
#define TARR_CLOCK_DEG   60.f
#define TARR_R           190
#define TARR_SLIDE_X     12
#define TARR_SLIDE_Y     21

typedef enum { ST_TURN = 0, ST_MODE, ST_MENU, ST_FLICK, ST_N } coach_step_t;

typedef struct {
    bool (*rect_fn)(lv_area_t *);
    int   hole_r;
    const char *caption;
    int   cap_y;
    screen_id_t dest;
} coach_step_def_t;

static const coach_step_def_t STEPS[ST_N] = {
    [ST_TURN]  = { NULL,             0,  "Turn to change temperature", COACH_CAP_Y_LOW, SCREEN_COUNT   },
    [ST_MODE]  = { home_pill_rect,   0,  "Tap to change mode",         0,               SCREEN_MODE    },
    [ST_MENU]  = { home_hero_rect,   24, "Tap or press for menu",      0,               SCREEN_MENU    },
    [ST_FLICK] = { home_airbtn_rect, 0,  "Swipe up for airflow",       216,             SCREEN_FANVANE },
};

static lv_obj_t    *s_grp;
static lv_obj_t    *s_ring;
static lv_obj_t    *s_tarr[2];
static lv_point_t   s_tarr_org[2];
static lv_point_t   s_tarr_vec[2];
static arim_t       s_arim_coach;
static int32_t      s_demo_deg;
static lv_timer_t  *s_poll;
static lv_timer_t  *s_wd;
static uint32_t     s_armed_tick;
static coach_step_t s_step;
static int          s_life;
static lv_area_t    s_rect;
static bool         s_pending;
static bool         s_turn_cw;
static bool         s_turn_ccw;
static uint32_t     s_turn_last;
static bool         s_closing;
static int          s_zone_snap;
static bool         s_has_hole;

static void step_show(coach_step_t st);
static void chain_end(void);
static bool face_settled(void);

static void coach_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void fade_obj(lv_obj_t *o, int32_t to, uint32_t ms, lv_anim_completed_cb_t done)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, coach_opa_cb);
    lv_anim_set_values(&a, lv_obj_get_style_opa(o, 0), to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

static void teardown(void)
{
    if (s_grp) { lv_obj_delete(s_grp); s_grp = NULL; }
    s_ring = NULL;
    s_tarr[0] = s_tarr[1] = NULL;
    s_closing = false;
}

static lv_obj_t *scrim_hole(const lv_area_t *hole, int hole_r)
{
    int32_t bw  = SERIN_DISP;
    int32_t hw  = hole->x2 - hole->x1 + 1;
    int32_t hh  = hole->y2 - hole->y1 + 1;
    int32_t r   = hole_r ? SERIN_PX(hole_r) : hh / 2;
    lv_obj_t *s = lv_obj_create(s_grp);
    lv_obj_remove_style_all(s);
    lv_obj_set_pos(s, hole->x1 - bw, hole->y1 - bw);
    lv_obj_set_size(s, hw + 2 * bw, hh + 2 * bw);
    lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s, r + bw, 0);
    lv_obj_set_style_border_width(s, bw, 0);
    lv_obj_set_style_border_color(s, lv_color_hex(COL_FACE), 0);
    lv_obj_set_style_border_opa(s, COACH_SCRIM_OPA, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    return s;
}

static lv_obj_t *scrim_full(void)
{
    lv_obj_t *s = lv_obj_create(s_grp);
    lv_obj_remove_style_all(s);
    lv_obj_set_size(s, SERIN_DISP, SERIN_DISP);
    lv_obj_set_style_bg_color(s, lv_color_hex(COL_FACE), 0);
    lv_obj_set_style_bg_opa(s, COACH_SCRIM_OPA, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE);
    return s;
}

static int32_t s_arrow_y0;

static lv_opa_t ramp_opa(int32_t v)
{
    return v < 38  ? (lv_opa_t)((LV_OPA_COVER * v) / 38)
         : v > 192 ? (lv_opa_t)((LV_OPA_COVER * (256 - v)) / 64)
         : LV_OPA_COVER;
}

static void arrow_exec(void *var, int32_t v)
{
    lv_obj_t *a = (lv_obj_t *)var;
    lv_obj_set_y(a, s_arrow_y0 - (SERIN_PX(ARROW_RISE_PX) * v) / 256);
    lv_obj_set_style_opa(a, ramp_opa(v), 0);
}

static void hint_loop_anim(lv_obj_t *o, lv_anim_exec_xcb_t exec)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, ARROW_RISE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&a, ARROW_LOOP_MS - ARROW_RISE_MS);
    lv_anim_start(&a);
}

static void tarr_exec(void *var, int32_t v)
{
    lv_obj_t *a = (lv_obj_t *)var;
    int i = (a == s_tarr[1]);
    lv_obj_set_pos(a, s_tarr_org[i].x + (s_tarr_vec[i].x * v) / 256,
                      s_tarr_org[i].y + (s_tarr_vec[i].y * v) / 256);
    lv_obj_set_style_opa(a, ramp_opa(v), 0);
}

static void tarr_hide(void)
{
    for (int i = 0; i < 2; i++)
        if (s_tarr[i]) { lv_obj_delete(s_tarr[i]); s_tarr[i] = NULL; }
}

static void tarr_show(bool cw)
{
    const serin_icon_t *ic[2] = { cw ? &coach_turn_cw_l : &coach_turn_ccw_l,
                                  cw ? &coach_turn_cw_r : &coach_turn_ccw_r };
    tarr_hide();
    for (int i = 0; i < 2; i++) {
        lv_obj_t *a = s_tarr[i] = mkicon(s_grp, ic[i], SERIN_PX(TARR_BOX), COL_WHITE);
        float clock = i ? TARR_CLOCK_DEG : -TARR_CLOCK_DEG;
        s_tarr_org[i].x = serin_px_x(clock, TARR_R) - SERIN_PX(TARR_BOX) / 2;
        s_tarr_org[i].y = serin_px_y(clock, TARR_R) - SERIN_PX(TARR_BOX) / 2;
        s_tarr_vec[i].x = cw ?  SERIN_PX(TARR_SLIDE_X) : -SERIN_PX(TARR_SLIDE_X);
        s_tarr_vec[i].y = SERIN_PX(TARR_SLIDE_Y) * (i ? 1 : -1) * (cw ? 1 : -1);
        hint_loop_anim(a, tarr_exec);
    }
}

static void step_show(coach_step_t st)
{
    const coach_step_def_t *d = &STEPS[st];
    lv_area_t hole;
    bool has_hole = d->rect_fn && d->rect_fn(&hole);
    if (d->rect_fn && !has_hole && st != ST_FLICK) {
        if (st + 1 < ST_N) { s_step = (coach_step_t)(st + 1); step_show(s_step); }
        else chain_end();
        return;
    }
    if (st == ST_FLICK && !airflow_available()) {
        chain_end();
        return;
    }

    s_step = st;
    s_zone_snap = s_zactive;
    s_has_hole = has_hole;
    lv_obj_t *g = s_grp = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, SERIN_DISP, SERIN_DISP);
    lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);

    int32_t cap_y;
    if (has_hole) {
        int32_t pad = SERIN_PX(COACH_HOLE_PAD);
        lv_area_t h = { hole.x1 - pad, hole.y1 - pad, hole.x2 + pad, hole.y2 + pad };
        s_rect = hole;
        scrim_hole(&h, d->hole_r);
        cap_y = d->cap_y ? SERIN_PX(d->cap_y) : h.y2 + SERIN_PX(COACH_CAP_GAP);
    } else {
        scrim_full();
        cap_y = SERIN_PX(d->cap_y);
        if (st == ST_TURN) {
            s_arim_coach = (arim_t){ .col = mode_accent(s_state.mode) };
            s_ring = arim_ring_create(g, &s_arim_coach);
            s_demo_deg = home_arc_fill_deg();
            arim_progress(s_ring, s_demo_deg, mode_accent(s_state.mode), false);
            s_turn_cw = s_turn_ccw = false;
            tarr_show(true);
        }
    }

    if (st == ST_FLICK) {
        lv_obj_t *ar = mkicon(g, &coach_arrow, SERIN_PX(ARROW_PX), COL_WHITE);
        lv_obj_align(ar, LV_ALIGN_TOP_MID, 0, SERIN_PX(ARROW_Y0));
        s_arrow_y0 = SERIN_PX(ARROW_Y0);
        hint_loop_anim(ar, arrow_exec);
    }

    lv_obj_t *cap = mklabel(g, FONT_WZ_HINT, COL_WHITE, d->caption);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, cap_y);

    s_life = COACH_STEP_LIFE_MS / COACH_WD_MS;
    lv_obj_set_style_opa(g, LV_OPA_TRANSP, 0);
    fade_obj(g, LV_OPA_COVER, s_wd ? COACH_XFADE_IN_MS : COACH_FADE_IN_MS, NULL);
}

static void xfade_out_cb(lv_anim_t *a)
{
    (void)a;
    teardown();
    if (!s_wd) return;
    if (!face_settled()) { chain_end(); return; }
    step_show(s_step);
}

static void advance(void)
{
    if (s_step + 1 >= ST_N) { chain_end(); return; }
    s_step = (coach_step_t)(s_step + 1);
    s_closing = true;
    fade_obj(s_grp, LV_OPA_TRANSP, COACH_XFADE_OUT_MS, xfade_out_cb);
}

static void end_out_cb(lv_anim_t *a) { (void)a; teardown(); }

static void chain_end(void)
{
    s_pending = false;
    if (s_wd)   { lv_timer_delete(s_wd); s_wd = NULL; }
    if (s_grp && !s_closing) {
        if (s_cur != SCREEN_THERMOSTAT) {
            teardown();
            return;
        }
        s_closing = true;
        fade_obj(s_grp, LV_OPA_TRANSP, COACH_FADE_OUT_MS, end_out_cb);
    }
}

static bool face_settled(void)
{
    return s_cur == SCREEN_THERMOSTAT && home_face_live()
        && s_state.alert == ALERT_NONE && !dlg_is_open() && !dlg_resetting();
}

static void wd_cb(lv_timer_t *t)
{
    (void)t;
    if (s_pending) {
        if (s_state.alert != ALERT_NONE || dlg_is_open() || dlg_resetting()
            || s_pair_state != PAIR_UI_HIDDEN) {
            chain_end();
            return;
        }
        if (face_settled() && input_quiet()
            && lv_display_get_inactive_time(NULL) < COACH_PRESENT_MS) {
            s_pending = false;
            step_show(s_step);
        }
        return;
    }
    if (!s_grp || s_closing) return;

    if (STEPS[s_step].dest != SCREEN_COUNT && s_cur == STEPS[s_step].dest) {
        if (s_step + 1 >= ST_N) { chain_end(); return; }
        s_step = (coach_step_t)(s_step + 1);
        s_pending = true;
        teardown();
        return;
    }

    if (!face_settled() || s_zactive != s_zone_snap) { chain_end(); return; }

    if (s_has_hole) {
        lv_area_t now;
        if (!STEPS[s_step].rect_fn(&now)
            || now.x1 != s_rect.x1 || now.y1 != s_rect.y1
            || now.x2 != s_rect.x2 || now.y2 != s_rect.y2) {
            chain_end();
            return;
        }
    }

    if (s_step == ST_TURN && s_turn_cw && s_turn_ccw
        && lv_tick_elaps(s_turn_last) >= COACH_TURN_SETTLE_MS) {
        advance();
        return;
    }

    if (--s_life <= 0) advance();
}

bool coach_sandbox_active(void)
{
    return s_grp && !s_closing && s_step == ST_TURN;
}

bool coach_demo_turn(int delta)
{
    if (!coach_sandbox_active()) return false;
    s_demo_deg += delta * COACH_RING_STEP_DEG;
    if (s_demo_deg < 0) s_demo_deg = 0;
    if (s_demo_deg > ARIM_SPAN_DEG) s_demo_deg = ARIM_SPAN_DEG;
    arim_progress(s_ring, s_demo_deg, mode_accent(s_state.mode), false);
    if (delta > 0 && !s_turn_cw) {
        s_turn_cw = true;
        if (s_turn_ccw) tarr_hide(); else tarr_show(false);
    } else if (delta < 0 && !s_turn_ccw) {
        s_turn_ccw = true;
        if (s_turn_cw) tarr_hide();
    }
    s_turn_last = lv_tick_get();
    s_life = COACH_STEP_LIFE_MS / COACH_WD_MS;
    return true;
}

void coach_dismiss(void)
{
    chain_end();
}

static bool coach_due(void)
{
    if (!face_settled()) return false;
    if (!input_quiet()) return false;
    return lv_display_get_inactive_time(NULL) < COACH_PRESENT_MS
        || lv_tick_elaps(s_armed_tick) < COACH_BOOT_GRACE_MS;
}

static void coach_poll_cb(lv_timer_t *t)
{
    (void)t;
    if (!coach_due()) return;
    lv_timer_delete(s_poll);
    s_poll = NULL;
    prefs_set_bool(COACH_KEY, true);
    s_wd = NULL;
    step_show(ST_TURN);
    s_wd = lv_timer_create(wd_cb, COACH_WD_MS, NULL);
}

void coach_init(void)
{
    if (s_poll || s_wd || s_grp) return;
    if (prefs_get_bool(COACH_KEY, false)) return;
    s_armed_tick = lv_tick_get();
    s_poll = lv_timer_create(coach_poll_cb, COACH_POLL_MS, NULL);
}

int ui_coach_active(void) { return s_grp != NULL; }

void ui_coach_arm(void)
{
    prefs_set_bool(COACH_KEY, false);
    coach_init();
}
