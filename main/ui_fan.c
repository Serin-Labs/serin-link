/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
#include "icons.h"
#include "sl2_proto.h"
#include <stdio.h>
#include <string.h>
#include "ui_gesture.h"
#include "serin_input.h"

static int          s_fan_sel = 1;
static bool         s_fan_editing = false;
static bool         s_fan_leave_pending;

static struct { lv_obj_t *vtile, *vimg, *vpill;
                lv_obj_t *ftile, *fbar, *fpill, *ficon;
                lv_obj_t *htile, *himg, *hpill;
                lv_obj_t *back_name, *back;            } F;

static void fan_focus_cb(lv_event_t *e);
static void vane_step(bool horiz, int delta, bool wrap);
static void fan_leave(void);

static const int      FAN_RIM_FOCUS[4] = { 2, 0, 1, -1 };
static const serin_icon_t *const FAN_RIM_ICON[4] = { &vane_h_auto, &mode_fan, &vane_v_auto, &nav_back };
static const int      FAN_RIM_ORDER[4] = { 1, 2, 3, 0 };
static int  fan_focus_stop(int f) { return f == 0 ? 1 : (f == 1 ? 2 : 0); }

static int     s_fan_disp        = 0;

#define VANE_CYCLE_MAX (SL2_UI_VANE_MAX_POS + 3)

static int vane_cycle(bool horiz, int out[VANE_CYCLE_MAX]) {
    uint8_t cap = horiz ? s_caps.vane_h : s_caps.vane_v;
    int n = 0;
    int split = horiz ? sl2_vanecap_split_pos(cap) : 0;
    int phys  = sl2_vanecap_npos(cap) - (split ? 1 : 0);
    if (phys > SL2_UI_VANE_MAX_POS) phys = SL2_UI_VANE_MAX_POS;
    if (phys < 0) phys = 0;
    if (!horiz && sl2_vanecap_has_auto(cap)) out[n++] = VANE_AUTO;
    for (int i = 1; i <= phys; i++)
        out[n++] = horiz ? HVANE_LEFT + (i - 1) : VANE_1 + (i - 1);
    if (split) out[n++] = HVANE_AUTO;
    if (sl2_vanecap_has_swing(cap)) out[n++] = horiz ? HVANE_SWING : VANE_SWING;
    if (n == 0) out[n++] = horiz ? HVANE_CENTER : VANE_3;
    return n;
}
static int vane_cycle_idx(const int *cyc, int n, int cur) {
    for (int i = 0; i < n; i++) if (cyc[i] == cur) return i;
    return 0;
}

static void vane_step(bool horiz, int delta, bool wrap)
{
    int cyc[VANE_CYCLE_MAX], n = vane_cycle(horiz, cyc);
    int cur = horiz ? (int)s_state.hvane : (int)s_state.vvane;
    int i = vane_cycle_idx(cyc, n, cur) + delta;
    if (wrap) i = ((i % n) + n) % n;
    else      { if (i < 0) i = 0; if (i >= n) i = n - 1; }
    if (horiz) s_state.hvane = (hvane_t)cyc[i];
    else       s_state.vvane = (vvane_t)cyc[i];
}

/* Stops name rim positions; focus names actual controls.  Keep the list in
 * one place so a capability change cannot make the rim, encoder and entry
 * disagree about what Airflow contains. */
static bool fan_control_available(int focus)
{
    switch (focus) {
        case 0: return s_caps.fan_steps > 0;
        case 1: return sl2_vanecap_npos(s_caps.vane_v) > 0;
        case 2: return sl2_vanecap_npos(s_caps.vane_h) > 0;
        default: return false;
    }
}

static int fan_control_stops(int out[3])
{
    int n = 0;
    /* Fan, vertical and horizontal is also the sensible replacement order. */
    if (fan_control_available(0)) out[n++] = 1;
    if (fan_control_available(1)) out[n++] = 2;
    if (fan_control_available(2)) out[n++] = 0;
    return n;
}

static int fan_navigation_stops(int out[4])
{
    int n = fan_control_stops(out);
    if (n > 1) out[n++] = 3;
    return n;
}

static int fan_ctrl_count(void) { int out[3]; return fan_control_stops(out); }

static bool fan_stop_active(int stop)
{
    int stops[4], n = fan_navigation_stops(stops);
    for (int i = 0; i < n; i++) if (stops[i] == stop) return true;
    return false;
}

static int fan_first_ctrl(void)
{
    int stops[3], n = fan_control_stops(stops);
    return n ? stops[0] : 3;
}

static int fan_next_stop(int sel, int delta)
{
    int stops[4], n = fan_navigation_stops(stops);
    if (!n) return 3;
    int i = 0;
    for (; i < n; i++) if (stops[i] == sel) break;
    if (i == n) i = 0;
    i = (i + delta) % n;
    if (i < 0) i += n;
    return stops[i];
}

static rim_t FR;

#define FAN_HGLYPH_IDLE  68
#define FAN_HGLYPH_SEL   90
#define FAN_HGLYPH_R    196

#define FAN_VANE_W      240
#define FAN_VANE_H      240
#define FAN_VANE_V_DX   (-15 * FAN_VANE_W / 128)
#define FAN_VANE_V_DY   ( 14 * FAN_VANE_W / 128)
#define FAN_BAR_W       104
#define FAN_BAR_H       250
#define FAN_BAR_R       (FAN_BAR_W / 2)
#define FAN_HVANE_W     262
#define FAN_HVANE_V_DY      0
#define FAN_HVANE_SWING_DY  37
#define FAN_AUTO_OPA    ((lv_opa_t)170)
#define FAN_AUTO_GHOST_OPA ((lv_opa_t)70)
#define FAN_HL_PAD      14
#define FAN_HL_R        24
#define FAN_ICON_PX     40
#define FAN_ICON_YOFF   11

static const serin_icon_t *const VANE_V_IMG[7] = {
    &vane_v_auto, &vane_v_5, &vane_v_4, &vane_v_3, &vane_v_2, &vane_v_1, &vane_v_swing };
static const serin_icon_t *const VANE_H_IMG[7] = {
    &vane_h_auto, &vane_h_left, &vane_h_cleft, &vane_h_center, &vane_h_cright, &vane_h_right, &vane_h_swing };
_Static_assert(sizeof VANE_V_IMG / sizeof *VANE_V_IMG == VANE_SWING  + 1, "vvane sprites");
_Static_assert(sizeof VANE_H_IMG / sizeof *VANE_H_IMG == HVANE_SWING + 1, "hvane sprites");

#define FAN_PCT_STEP 10
static int s_fan_pct = 60;
static int s_fan_last_manual = 60;
#define FAN_EDIT_HOLD_MS 1500
static uint32_t s_fan_edit_tick;
static bool     s_fan_edited;
static void fan_edited(void) { s_fan_edit_tick = lv_tick_get(); s_fan_edited = true; }
static int fan_band_pct(int k) {
    int n = s_caps.fan_steps ? s_caps.fan_steps : 5;
    return (k * 100 + n / 2) / n;
}
static int fan_quant(int p) {
    if (p <= 0) return 0;
    int n = s_caps.fan_steps ? s_caps.fan_steps : 5;
    int k = (p * n + 50) / 100;
    return fan_band_pct(k < 1 ? 1 : (k > n ? n : k));
}
static int fan_pct_floor(void) { return s_caps.fan_has_auto ? 0 : 1; }

static lv_obj_t *auto_pill(lv_obj_t *holder)
{
    lv_obj_t *l = mklabel(holder, FONT_DIAL_CHIP, COL_WHITE, "AUTO");
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, SERIN_PX(999), 0);
    lv_obj_set_style_pad_hor(l, SERIN_PX(14), 0);
    lv_obj_set_style_pad_ver(l, SERIN_PX(6), 0);
    lv_obj_center(l);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}
static void auto_pill_set(lv_obj_t *l, uint32_t col, bool show)
{
    if (!show) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_set_style_bg_color(l, lv_color_hex(col), 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *fan_tile(lv_obj_t *p, int w, int h, int f)
{
    lv_obj_t *t = lv_obj_create(p);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, SERIN_PX(w + 2 * FAN_HL_PAD), SERIN_PX(h + 2 * FAN_HL_PAD));
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_user_data(t, (void *)(intptr_t)f);
    lv_obj_add_event_cb(t, fan_focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_radius(t, SERIN_PX(FAN_HL_R), LV_PART_MAIN);
    press_ack(t, ACK_OPA_TILE);
    return t;
}

/* The fan bar and the two vane tiles are one gesture: past a little slack the
 * finger takes the control over, then every step_px of travel moves the value
 * one step and a throttled edit goes out.  They differ only in the axis they
 * read, what a step costs and what a step does. */
typedef struct {
    bool  horiz;        /* slide along x when set, along y otherwise */
    int   dir;          /* +1 when travel along the axis raises the value */
    float step_px;      /* travel one step of the value costs */
    int   focus;        /* control the slide arms: 0 fan, 1 vane V, 2 vane H */
    bool  keep_slack;   /* spend the travel that engaged it on the first step */
    bool  cross_eats;   /* a slide across the axis is a drag too, not a tap */
    bool (*apply)(int steps);   /* step the value; true when it moved */
} fan_drag_t;

static struct {
    const fan_drag_t *cfg;
    bool     engaged;
    int      last;      /* last position along the slide axis */
    int      cross0;    /* press position across it */
    float    accum;     /* travel not yet spent on a step */
    uint32_t last_tx;
} s_fdrag;

static uig_t *s_fbar_gest, *s_vvane_gest, *s_hvane_gest;

static bool fan_drag_arm(const fan_drag_t *cfg, lv_point_t p)
{
    s_fdrag.cfg     = cfg;
    s_fdrag.engaged = false;
    s_fdrag.last    = cfg->horiz ? p.x : p.y;
    s_fdrag.cross0  = cfg->horiz ? p.y : p.x;
    s_fdrag.accum   = 0.0f;
    return true;
}

/* may_engage is ignored: these tiles arm no swipe of their own, so the gesture
 * layer never holds one of their presses back as a swipe candidate. */
static bool fan_drag_frame(lv_point_t p, bool may_engage)
{
    (void)may_engage;
    const fan_drag_t *c = s_fdrag.cfg;
    int pos = c->horiz ? p.x : p.y;
    int d   = pos - s_fdrag.last;
    s_fdrag.last   = pos;
    s_fdrag.accum += (float)d;

    if (!s_fdrag.engaged) {
        int e = SERIN_PX(SERIN_FBAR_ENGAGE_PX);
        if (s_fdrag.accum < (float)e && s_fdrag.accum > -(float)e) {
            if (!c->cross_eats) return false;
            /* A slide across the tile's axis changes nothing, and is not a tap. */
            int cr = (c->horiz ? p.y : p.x) - s_fdrag.cross0;
            return cr >= e || cr <= -e;
        }
        s_fdrag.engaged = true;
        /* The bar starts counting where it engaged, so it cannot jump. */
        if (!c->keep_slack) s_fdrag.accum = (float)d;
        note_activity();
        s_fan_sel = fan_focus_stop(c->focus);
        s_fan_editing = true;
        refresh_fanvane();
    }

    int steps = (int)(s_fdrag.accum / c->step_px);
    if (!steps) return true;
    s_fdrag.accum -= (float)steps * c->step_px;
    if (!c->apply(steps * c->dir)) return true;
    refresh_fanvane();
    if (lv_tick_elaps(s_fdrag.last_tx) >= SERIN_DRAG_TX_MS) {
        notify_edit();
        s_fdrag.last_tx = lv_tick_get();
    }
    return true;
}

static void fan_drag_end(void)
{
    /* A slide that never took the control over has nothing to send. */
    if (s_fdrag.engaged) notify_edit();
}

static bool fan_bar_apply(int steps)
{
    int pct = s_fan_pct + steps;
    if (pct < fan_pct_floor()) pct = fan_pct_floor();
    if (pct > 100) pct = 100;
    if (pct == s_fan_pct) return false;
    s_fan_pct = pct;
    s_state.fan_pct = (uint8_t)fan_quant(pct);
    fan_edited();
    return true;
}

/* Each vane tile slides along the axis its positions are drawn on: the
 * vertical vane up/down, the horizontal vane left/right.  Steps follow the
 * knob's cycle order and stop at the ends. */
static bool vane_drag_apply(int steps)
{
    bool horiz = s_fdrag.cfg->horiz;
    int before = horiz ? (int)s_state.hvane : (int)s_state.vvane;
    vane_step(horiz, steps, false);
    return (horiz ? (int)s_state.hvane : (int)s_state.vvane) != before;
}

static const fan_drag_t FAN_BAR_DRAG = {
    .dir = -1, .step_px = SERIN_PX(FAN_BAR_H) / 100.0f,
    .focus = 0, .apply = fan_bar_apply,
};
static const fan_drag_t VVANE_DRAG = {
    .dir = +1, .step_px = SERIN_PX(SERIN_VANE_STEP_PX),
    .focus = 1, .keep_slack = true, .cross_eats = true, .apply = vane_drag_apply,
};
static const fan_drag_t HVANE_DRAG = {
    .horiz = true, .dir = +1, .step_px = SERIN_PX(SERIN_VANE_STEP_PX),
    .focus = 2, .keep_slack = true, .cross_eats = true, .apply = vane_drag_apply,
};

static bool fan_bar_arm(lv_point_t p)    { return fan_drag_arm(&FAN_BAR_DRAG, p); }
static bool vvane_drag_arm(lv_point_t p) { return fan_drag_arm(&VVANE_DRAG, p); }
static bool hvane_drag_arm(lv_point_t p) { return fan_drag_arm(&HVANE_DRAG, p); }

static const uig_cfg_t FBAR_GESTURES = {
    .drag_arm      = fan_bar_arm,
    .on_drag_frame = fan_drag_frame,
    .on_drag_end   = fan_drag_end,
};
static const uig_cfg_t VVANE_GESTURES = {
    .drag_arm      = vvane_drag_arm,
    .on_drag_frame = fan_drag_frame,
    .on_drag_end   = fan_drag_end,
};
static const uig_cfg_t HVANE_GESTURES = {
    .drag_arm      = hvane_drag_arm,
    .on_drag_frame = fan_drag_frame,
    .on_drag_end   = fan_drag_end,
};

static void fan_panel_swipe_v(int dir)
{
    if (dir >= 0) return;
    note_activity();
    ui_back();
}

static const uig_cfg_t FAN_PANEL_GESTURES = {
    .swipe_min_px    = SERIN_SWIPE_MIN_PX,
    .swipe_dial_px   = SERIN_SWIPE_DIAL_PX,
    .swipe_dom       = SERIN_SWIPE_DOM,
    .swipe_window_ms = SERIN_SWIPE_WINDOW_MS,
    .on_swipe_v      = fan_panel_swipe_v,
};

static void fan_rim_click_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (FAN_RIM_FOCUS[i] < 0) { ui_back(); return; }
    s_fan_sel = i;
    s_fan_editing = true;
    refresh_fanvane();
}

static void fan_return_cb(lv_event_t *e)
{
    (void)e;
    ui_back();
}

void build_fanvane(void)
{
    lv_obj_t *p = s_panel[SCREEN_FANVANE] = mkpanel(COL_FACE, false);

    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    (void)uig_attach(p, &FAN_PANEL_GESTURES);

    F.vtile = fan_tile(p, FAN_VANE_W, FAN_VANE_H, 1);
    lv_obj_center(F.vtile);
    F.vimg  = mkicon(F.vtile, &vane_v_auto, SERIN_PX(FAN_VANE_W), COL_GREY);
    lv_obj_align(F.vimg, LV_ALIGN_CENTER, SERIN_PX(FAN_VANE_V_DX), SERIN_PX(FAN_VANE_V_DY));
    F.vpill = auto_pill(F.vtile);
    s_vvane_gest = uig_attach(F.vtile, &VVANE_GESTURES);

    F.ftile = fan_tile(p, FAN_BAR_W, FAN_BAR_H, 0);
    lv_obj_center(F.ftile);
    lv_obj_t *fb = F.fbar = lv_bar_create(F.ftile);
    lv_obj_remove_style_all(fb);
    lv_obj_set_size(fb, SERIN_PX(FAN_BAR_W), SERIN_PX(FAN_BAR_H));
    lv_obj_center(fb);
    lv_bar_set_orientation(fb, LV_BAR_ORIENTATION_VERTICAL);
    lv_bar_set_range(fb, 0, 100);
    lv_obj_remove_flag(fb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(fb, SERIN_PX(FAN_BAR_R), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fb, (lv_opa_t)41, LV_PART_MAIN);
    lv_obj_set_style_radius(fb, SERIN_PX(FAN_BAR_R), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(fb, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(fb, 180, 0);
    F.fpill = auto_pill(F.ftile);
    F.ficon = mkicon(fb, &mode_fan, SERIN_PX(FAN_ICON_PX), COL_WHITE);
    lv_obj_align(F.ficon, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(FAN_ICON_YOFF));
    s_fbar_gest = uig_attach(F.ftile, &FBAR_GESTURES);

    F.htile = fan_tile(p, FAN_HVANE_W, FAN_HVANE_W, 2);
    lv_obj_center(F.htile);
    F.himg  = mkicon(F.htile, &vane_h_auto, SERIN_PX(FAN_HVANE_W), COL_GREY);
    lv_obj_align(F.himg, LV_ALIGN_CENTER, 0, SERIN_PX(FAN_HVANE_V_DY));
    F.hpill = auto_pill(F.htile);
    s_hvane_gest = uig_attach(F.htile, &HVANE_GESTURES);

    float ang[4]; uint32_t col[4]; const serin_icon_t *gly[4];
    for (int k = 0; k < 4; k++) {
        int st = FAN_RIM_ORDER[k];
        ang[st] = k * 90.0f;
        col[st] = (st == 3) ? COL_BACK : COL_SYSTEM;
        gly[st] = FAN_RIM_ICON[st];
    }
    static const uint8_t hpx_idle[4] = { FAN_HGLYPH_IDLE, 0, 0, 0 };
    static const uint8_t hpx_sel[4]  = { FAN_HGLYPH_SEL,  0, 0, 0 };
    rim_cfg_t cfg = {
        .n = 4, .ang = ang, .col = col, .glyph = gly,
        .half_deg = 37.0f, .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 45, .disp_sel = 60, .px_idle = hpx_idle, .px_sel = hpx_sel,
        .node_px = 100, .on_tap = fan_rim_click_cb,
        .no_ind = true,
    };
    rim_build(&FR, p, &cfg);
    F.back_name = mkslot_label(p, FONT_SET_NAME, COL_BACK, "Back", 260, SERIN_SLOT_TITLE_Y);
    F.back = mkslot_action(p, "Return", fan_return_cb, NULL);
    lv_obj_add_flag(F.back_name, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(F.back, LV_OBJ_FLAG_HIDDEN);
}

#define FAN_SEG_HALF_MAX 26.0f
static void fan_rim_layout(void)
{
    int act[4], n = fan_navigation_stops(act);

    if (n <= 1) { rim_hide(&FR, true); return; }

    rim_hide(&FR, false);
    for (int st = 0; st < 4; st++)
        if (!fan_stop_active(st)) {
            lv_obj_add_flag(FR.seg[st],  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(FR.node[st], LV_OBJ_FLAG_HIDDEN);
        }

    float pitch = 360.0f / (float)n;
    float half  = pitch / 2.0f - 8.0f;
    if (n < 4 && half > FAN_SEG_HALF_MAX) half = FAN_SEG_HALF_MAX;
    int   np    = SERIN_PX(100);
    int back_idx = -1;
    for (int k = 0; k < n; k++) if (act[k] == 3) back_idx = k;
    for (int k = 0; k < n; k++) {
        int st = act[k];
        float a = FR.ang[st] = rim_stop_angle(k, n, back_idx);
        rim_set_span(FR.seg[st], a - half, a + half);
        int gr = (st == 0) ? FAN_HGLYPH_R : SERIN_GLYPH_R_SET;
        lv_obj_set_pos(FR.node[st], serin_px_x(a, gr) - np / 2,
                                    serin_px_y(a, gr) - np / 2);
    }
}

static bool fan_normalize_selection(void)
{
    int n = fan_ctrl_count();
    if (!n) return false;
    if (!fan_stop_active(s_fan_sel)) {
        s_fan_sel = fan_first_ctrl();
        s_fan_editing = n == 1;
    }
    if (n == 1) {
        s_fan_sel = fan_first_ctrl();
        s_fan_editing = true;
    }
    return true;
}

static void fan_leave_cb(void *arg)
{
    (void)arg;
    s_fan_leave_pending = false;
    if (s_cur == SCREEN_FANVANE && !airflow_available())
        ui_back();
}

static void fan_leave(void)
{
    if (s_fan_leave_pending) return;
    s_fan_leave_pending = true;
    lv_async_call(fan_leave_cb, NULL);
}

void refresh_fanvane(void)
{
    if (!fan_normalize_selection()) { fan_leave(); return; }
    fan_rim_layout();
    const hvac_state_t *s = &s_state;
    uint32_t accent = COL_SYSTEM;
    int armed = FAN_RIM_FOCUS[s_fan_sel];

    if (armed >= 0) s_fan_disp = armed;
    if (!fan_control_available(s_fan_disp)) {
        int fc = FAN_RIM_FOCUS[fan_first_ctrl()];
        s_fan_disp = fc >= 0 ? fc : 0;
    }
    lv_obj_t *tiles[3] = { F.ftile, F.vtile, F.htile };
    for (int f = 0; f < 3; f++) {
        if (armed >= 0 && f == s_fan_disp) lv_obj_remove_flag(tiles[f], LV_OBJ_FLAG_HIDDEN);
        else                             lv_obj_add_flag(tiles[f], LV_OBJ_FLAG_HIDDEN);
    }
    if (armed < 0) {
        lv_obj_remove_flag(F.back_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(F.back, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(F.back_name, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(F.back, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t vCol = (armed == 1) ? accent : COL_GREY;
    uint32_t fCol = (armed == 0) ? accent : COL_GREY;
    uint32_t hCol = (armed == 2) ? accent : COL_GREY;

    icon_set(F.vimg, VANE_V_IMG[s->vvane], vCol);
    lv_obj_set_style_image_opa(F.vimg, s->vvane == VANE_AUTO ? FAN_AUTO_GHOST_OPA
                                                             : LV_OPA_COVER, 0);
    auto_pill_set(F.vpill, vCol, s->vvane == VANE_AUTO);

    bool fanAuto = (s->fan_pct == 0);
    lv_color_t fc = lv_color_hex(fCol);
    lv_obj_set_style_bg_color(F.fbar, fc, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(F.fbar, fanAuto ? FAN_AUTO_OPA : (lv_opa_t)41, LV_PART_MAIN);
    lv_obj_set_style_bg_color(F.fbar, fc, LV_PART_INDICATOR);
    lv_bar_set_value(F.fbar, s_fan_pct, LV_ANIM_ON);
    auto_pill_set(F.fpill, fCol, fanAuto);

    icon_set(F.himg, VANE_H_IMG[s->hvane], hCol);
    lv_obj_align(F.himg, LV_ALIGN_CENTER, 0,
                 (s->hvane == HVANE_SWING) ? SERIN_PX(FAN_HVANE_SWING_DY)
                                           : SERIN_PX(FAN_HVANE_V_DY));
    lv_obj_set_style_image_opa(F.himg, s->hvane == HVANE_AUTO ? FAN_AUTO_GHOST_OPA
                                                              : LV_OPA_COVER, 0);
    auto_pill_set(F.hpill, hCol, s->hvane == HVANE_AUTO);

    for (int i = 0; i < 4; i++) {
        uint32_t c = (i == 3) ? COL_BACK : accent;
        FR.col[i] = c;
        rim_tint(&FR, i, c);
    }
    rim_select(&FR, s_fan_sel, armed < 0 ? COL_BACK : accent);
}

void fanvane_press(void)
{
    if (!fan_normalize_selection()) { ui_back(); return; }
    if (fan_ctrl_count() <= 1) { ui_back(); return; }
    if (FAN_RIM_FOCUS[s_fan_sel] < 0) { ui_back(); return; }
    s_fan_editing = !s_fan_editing;
    refresh_fanvane();
}

static void fan_focus_cb(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target(e);
    int f = (int)(intptr_t)lv_obj_get_user_data(t);
    const uig_t *g = t == F.ftile ? s_fbar_gest
                   : t == F.vtile ? s_vvane_gest
                   : t == F.htile ? s_hvane_gest : NULL;
    if (g && !uig_click_is_tap(g)) return;
    int stop = fan_focus_stop(f);
    if (s_fan_sel == stop && s_fan_editing) {
        if      (f == 1) { vane_step(false, +1, true); notify_edit(); }
        else if (f == 2) { vane_step(true,  +1, true); notify_edit(); }
        else if (f == 0 && s_caps.fan_has_auto) {
            if (s_fan_pct <= 0) s_fan_pct = s_fan_last_manual > 0 ? s_fan_last_manual : 60;
            else { s_fan_last_manual = s_fan_pct; s_fan_pct = 0; }
            s_state.fan_pct = (uint8_t)fan_quant(s_fan_pct);
            fan_edited();
            notify_edit();
        }
        refresh_fanvane();
        return;
    }
    s_fan_sel = stop;
    s_fan_editing = true;
    refresh_fanvane();
}

void fan_adopt(uint8_t pct)
{
    if (s_fan_edited && lv_tick_elaps(s_fan_edit_tick) < FAN_EDIT_HOLD_MS) return;
    if (fan_quant(s_fan_pct) != pct) s_fan_pct = pct;
}

void fanvane_enter(void)
{
    /* Do not rely on a cached configuration: caps may have changed while the
     * screen was away.  Entry always starts by directly editing the first
     * control Airflow can actually offer. */
    if (!fan_normalize_selection()) { fan_leave(); return; }
    s_fan_sel = fan_first_ctrl();
    s_fan_editing = true;
}

void fanvane_caps_changed(void)
{
    if (s_cur != SCREEN_FANVANE || !s_panel[SCREEN_FANVANE]) return;
    if (!fan_normalize_selection()) { ui_back(); return; }
    refresh_fanvane();
}

void fan_turn(int delta)
{
    int armed = FAN_RIM_FOCUS[s_fan_sel];
    if (!s_fan_editing || armed < 0) {
        s_fan_sel = fan_next_stop(s_fan_sel, delta);
        refresh_fanvane();
    } else {
        if (armed == 0) {
            int p = s_fan_pct + delta * FAN_PCT_STEP;
            if (p < fan_pct_floor()) p = fan_pct_floor();
            if (p > 100) p = 100;
            s_fan_pct = p;
            s_state.fan_pct = (uint8_t)fan_quant(p);
            fan_edited();
        } else if (armed == 1) {
            vane_step(false, delta, false);
        } else {
            vane_step(true, delta, false);
        }
        refresh_fanvane();
        notify_edit();
    }
}

bool airflow_available(void)
{
    return fan_ctrl_count() > 0;
}
