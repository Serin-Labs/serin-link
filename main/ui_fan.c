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

static struct { lv_obj_t *vtile, *vimg, *vpill;
                lv_obj_t *ftile, *fbar, *fpill, *ficon;
                lv_obj_t *htile, *himg, *hpill;          } F;

static void fan_focus_cb(lv_event_t *e);
static void vane_step(bool horiz, int delta, bool wrap);
static uint8_t fan_vane_cfg(void);

static const int      FAN_RIM_FOCUS[4] = { 2, 0, 1, -1 };
static const serin_icon_t *const FAN_RIM_ICON[4] = { &vane_h_auto, &mode_fan, &vane_v_auto, &nav_back };
static const int      FAN_RIM_ORDER[4] = { 1, 2, 3, 0 };
static int  fan_focus_stop(int f) { return f == 0 ? 1 : (f == 1 ? 2 : 0); }

static uint8_t s_fan_cfg_applied = 0xFF;
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

static bool fan_stop_active(int stop, uint8_t cfg) {
    if (stop == 0) return cfg >= 2;
    if (stop == 1) return s_caps.fan_steps > 0;
    if (stop == 2) return cfg >= 1;
    return true;
}
static int fan_ctrl_count(uint8_t cfg) {
    return (fan_stop_active(0, cfg) ? 1 : 0) + (fan_stop_active(1, cfg) ? 1 : 0)
         + (fan_stop_active(2, cfg) ? 1 : 0);
}
static int fan_first_ctrl(uint8_t cfg) {
    if (fan_stop_active(1, cfg)) return 1;
    if (fan_stop_active(2, cfg)) return 2;
    if (fan_stop_active(0, cfg)) return 0;
    return 3;
}

static int fan_next_stop(int sel, int delta, uint8_t cfg) {
    int dir = (delta >= 0) ? 1 : -1;
    for (int n = (delta >= 0) ? delta : -delta; n > 0; n--)
        do { sel = (sel + dir + 4) % 4; } while (!fan_stop_active(sel, cfg));
    return sel;
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

static bool     s_fbar_engaged;
static int      s_fbar_y0;
static int      s_fbar_y;
static float    s_fbar_accum;
static uint32_t s_fbar_last_tx;
static uig_t   *s_fbar_gest;

static bool fan_bar_arm(lv_point_t p)
{
    s_fbar_engaged = false;
    s_fbar_y0 = s_fbar_y = p.y;
    s_fbar_accum = 0.0f;
    return true;
}

static bool fan_bar_frame(lv_point_t p, bool may_engage)
{
    (void)may_engage;
    int dy = p.y - s_fbar_y;
    s_fbar_y = p.y;

    if (!s_fbar_engaged) {
        int t = p.y - s_fbar_y0;
        if (t < SERIN_PX(SERIN_FBAR_ENGAGE_PX) && t > -SERIN_PX(SERIN_FBAR_ENGAGE_PX)) return false;
        s_fbar_engaged = true;
        note_activity();
        s_fan_sel = 1;
        s_fan_editing = true;
    }

    s_fbar_accum += -(float)dy * 100.0f / (float)SERIN_PX(FAN_BAR_H);
    int step = (int)s_fbar_accum;
    if (!step) return true;
    s_fbar_accum -= (float)step;
    int pct = s_fan_pct + step;
    if (pct < fan_pct_floor()) pct = fan_pct_floor();
    if (pct > 100) pct = 100;
    if (pct == s_fan_pct) return true;
    s_fan_pct = pct;
    s_state.fan_pct = (uint8_t)fan_quant(pct);
    fan_edited();
    refresh_fanvane();
    if (lv_tick_elaps(s_fbar_last_tx) >= SERIN_DRAG_TX_MS) {
        notify_edit();
        s_fbar_last_tx = lv_tick_get();
    }
    return true;
}

static void fan_bar_end(void)
{
    notify_edit();
}

static const uig_cfg_t FBAR_GESTURES = {
    .drag_arm      = fan_bar_arm,
    .on_drag_frame = fan_bar_frame,
    .on_drag_end   = fan_bar_end,
};

static void fan_panel_swipe_v(int dir)
{
    if (dir >= 0) return;
    note_activity();
    ui_show_screen(SCREEN_THERMOSTAT);
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
    if (FAN_RIM_FOCUS[i] < 0) { ui_show_screen(SCREEN_THERMOSTAT); return; }
    s_fan_sel = i;
    s_fan_editing = true;
    refresh_fanvane();
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
}

#define FAN_SEG_HALF_MAX 26.0f
static void fan_rim_layout(uint8_t cfg)
{
    int act[4], n = 0;
    for (int k = 0; k < 4; k++)
        if (fan_stop_active(FAN_RIM_ORDER[k], cfg)) act[n++] = FAN_RIM_ORDER[k];

    if (n <= 1) { rim_hide(&FR, true); return; }

    rim_hide(&FR, false);
    for (int st = 0; st < 4; st++)
        if (!fan_stop_active(st, cfg)) {
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

static void fan_apply_config(uint8_t cfg)
{
    if (cfg > 2) cfg = 2;
    uint8_t sig = (uint8_t)(cfg | (s_caps.fan_steps ? 0x10 : 0));
    if (sig == s_fan_cfg_applied) return;
    s_fan_cfg_applied = sig;

    fan_rim_layout(cfg);

    if (!fan_stop_active(s_fan_sel, cfg)) { s_fan_sel = fan_first_ctrl(cfg); s_fan_editing = false; }
    if (fan_ctrl_count(cfg) <= 1) {
        s_fan_sel = fan_first_ctrl(cfg);
        s_fan_editing = (FAN_RIM_FOCUS[s_fan_sel] >= 0);
    }
}

void refresh_fanvane(void)
{
    fan_apply_config(fan_vane_cfg());
    const hvac_state_t *s = &s_state;
    uint32_t accent = COL_SYSTEM;
    int armed = FAN_RIM_FOCUS[s_fan_sel];

    if (armed >= 0) s_fan_disp = armed;
    uint8_t cfg = fan_vane_cfg();
    if (!fan_stop_active(fan_focus_stop(s_fan_disp), cfg)) {
        int fc = FAN_RIM_FOCUS[fan_first_ctrl(cfg)];
        s_fan_disp = fc >= 0 ? fc : 0;
    }
    lv_obj_t *tiles[3] = { F.ftile, F.vtile, F.htile };
    for (int f = 0; f < 3; f++) {
        if (f == s_fan_disp) lv_obj_remove_flag(tiles[f], LV_OBJ_FLAG_HIDDEN);
        else                 lv_obj_add_flag(tiles[f], LV_OBJ_FLAG_HIDDEN);
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
    rim_select(&FR, s_fan_sel, accent);
}

void fanvane_press(void)
{
    uint8_t cfg = fan_vane_cfg();
    if (fan_ctrl_count(cfg) <= 1) { ui_show_screen(SCREEN_THERMOSTAT); return; }
    if (FAN_RIM_FOCUS[s_fan_sel] < 0) { ui_show_screen(SCREEN_THERMOSTAT); return; }
    s_fan_editing = !s_fan_editing;
    refresh_fanvane();
}

static void fan_focus_cb(lv_event_t *e)
{
    int f = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (s_fbar_gest && lv_event_get_target(e) == F.ftile
        && !uig_click_is_tap(s_fbar_gest)) return;
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

void fanvane_enter(void)  { s_fan_sel = 1; s_fan_editing = true; }

void fanvane_caps_changed(void)
{
    if (s_cur == SCREEN_FANVANE && s_panel[SCREEN_FANVANE]) refresh_fanvane();
}

void fan_turn(int delta)
{
    int armed = FAN_RIM_FOCUS[s_fan_sel];
    if (!s_fan_editing || armed < 0) {
        s_fan_sel = fan_next_stop(s_fan_sel, delta, fan_vane_cfg());
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
    return s_caps.fan_steps > 0 || fan_vane_cfg() != 0;
}

static uint8_t fan_vane_cfg(void) {
    uint8_t nv = sl2_vanecap_npos(s_caps.vane_v), nh = sl2_vanecap_npos(s_caps.vane_h);
    return nv ? (nh ? 2 : 1) : 0;
}
