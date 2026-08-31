/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_arim.h"
#include "ui_theme.h"
#include "serin_tokens.h"
#include "ui_setif.h"
#include <math.h>

arim_t s_arim_home = { .col = COL_ALERT_NOLINK, .opa = LV_OPA_COVER };
arim_t s_arim_dlg  = { .col = COL_ALERT_NOLINK, .opa = LV_OPA_COVER };

static arim_t *arim_of(lv_obj_t *ring) { return (arim_t *)lv_obj_get_user_data(ring); }

static void arim_breath_stop(lv_obj_t *ring);

static void arim_draw_cb(lv_event_t *e)
{
    lv_obj_t *ring = lv_event_get_target_obj(e);
    arim_t *r = arim_of(ring);
    if (!r) return;
    lv_area_t coords;
    lv_obj_get_coords(ring, &coords);

    int32_t w = coords.x2 - coords.x1 + 1;
    int32_t h = coords.y2 - coords.y1 + 1;
    lv_point_t ac = { coords.x1 + w / 2, coords.y1 + h / 2 };

    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.center  = ac;
    d.radius  = ARIM_ARC_R_OUT;
    d.width   = SERIN_PX(ARIM_ARC_W);
    d.rounded = 1;
    d.opa     = LV_OPA_COVER;

    d.color       = lv_color_hex(COL_TRACK);
    d.start_angle = ARIM_ARC_A0;
    d.end_angle   = ARIM_ARC_A1;
    lv_draw_arc(lv_event_get_layer(e), &d);

    d.color = lv_color_hex(r->col);
    if (r->searching) {
        int32_t s  = r->sweep_deg;
        int32_t e2 = s + PAIR_SWEEP_DEG;
        if (s  < ARIM_ARC_A0) s  = ARIM_ARC_A0;
        if (e2 > ARIM_ARC_A1) e2 = ARIM_ARC_A1;
        if (e2 <= s) return;
        d.start_angle = s;
        d.end_angle   = e2;
    } else if (r->progress) {
        if (r->prog_deg <= 0) return;
        int32_t e2 = ARIM_ARC_A0 + r->prog_deg;
        if (e2 > ARIM_ARC_A1) e2 = ARIM_ARC_A1;
        d.start_angle = ARIM_ARC_A0;
        d.end_angle   = e2;
    } else {
        d.start_angle = ARIM_ARC_A0;
        d.end_angle   = ARIM_ARC_A1;
        d.opa = r->breathing ? (lv_opa_t)r->opa : LV_OPA_COVER;
    }
    lv_draw_arc(lv_event_get_layer(e), &d);
}

static void arim_frame(lv_obj_t *ring)
{
    lv_area_t c;
    lv_obj_get_coords(ring, &c);
    lv_coord_t f = (c.x2 - c.x1 + 1) / 2
                 - SERIN_PX(ARIM_BAND_R_IN * 68 / 100);
    if (f < 1) { lv_obj_invalidate(ring); return; }
    lv_area_t top   = { c.x1,     c.y1,         c.x2,     c.y1 + f };
    lv_area_t left  = { c.x1,     c.y1 + f + 1, c.x1 + f, c.y2 };
    lv_area_t right = { c.x2 - f, c.y1 + f + 1, c.x2,     c.y2 };
    lv_obj_invalidate_area(ring, &top);
    lv_obj_invalidate_area(ring, &left);
    lv_obj_invalidate_area(ring, &right);
}

static void arim_sector_bbox(lv_point_t ctr, int32_t r_in, int32_t r_out,
                             int32_t a0, int32_t a1, lv_area_t *out)
{
    int32_t x1 = INT32_MAX, y1 = INT32_MAX, x2 = INT32_MIN, y2 = INT32_MIN;
    #define ARIM_PT(px, py) do {                                     \
        int32_t px_ = (px), py_ = (py);                              \
        if (px_ < x1) x1 = px_;                                      \
        if (px_ > x2) x2 = px_;                                      \
        if (py_ < y1) y1 = py_;                                      \
        if (py_ > y2) y2 = py_;                                      \
    } while (0)

    for (int e = 0; e < 2; e++) {
        float a  = (e ? a1 : a0) * (float)M_PI / 180.0f;
        float ca = cosf(a), sa = sinf(a);
        ARIM_PT(ctr.x + (int32_t)floorf(r_in  * ca), ctr.y + (int32_t)floorf(r_in  * sa));
        ARIM_PT(ctr.x + (int32_t)ceilf (r_in  * ca), ctr.y + (int32_t)ceilf (r_in  * sa));
        ARIM_PT(ctr.x + (int32_t)floorf(r_out * ca), ctr.y + (int32_t)floorf(r_out * sa));
        ARIM_PT(ctr.x + (int32_t)ceilf (r_out * ca), ctr.y + (int32_t)ceilf (r_out * sa));
    }
    for (int32_t q = ((a0 + 89) / 90) * 90; q <= a1; q += 90) {
        float a = q * (float)M_PI / 180.0f;
        ARIM_PT(ctr.x + (int32_t)floorf(r_out * cosf(a)), ctr.y + (int32_t)floorf(r_out * sinf(a)));
        ARIM_PT(ctr.x + (int32_t)ceilf (r_out * cosf(a)), ctr.y + (int32_t)ceilf (r_out * sinf(a)));
    }
    #undef ARIM_PT
    out->x1 = x1; out->y1 = y1; out->x2 = x2; out->y2 = y2;
}

static void arim_invalidate_anim(lv_obj_t *ring)
{
    arim_t *r = arim_of(ring);
    if (!r) return;

    if (!r->searching) { arim_frame(ring); return; }

    lv_area_t c;
    lv_obj_get_coords(ring, &c);
    int32_t bw = c.x2 - c.x1 + 1, bh = c.y2 - c.y1 + 1;
    lv_point_t ac   = { c.x1 + bw / 2, c.y1 + bh / 2 };
    int32_t    half = SERIN_PX(ARIM_ARC_W) / 2;
    int32_t    rout = SERIN_PX(ARIM_ARC_R) + half;
    int32_t    rin  = SERIN_PX(ARIM_ARC_R) - half;

    int32_t prev = r->inval_deg, now = r->sweep_deg;
    r->inval_deg = now;
    int32_t a0 = prev;
    int32_t a1 = (now < prev) ? ARIM_ARC_A1 : now + PAIR_SWEEP_DEG;

    if (a0 < ARIM_ARC_A0) a0 = ARIM_ARC_A0;
    if (a1 > ARIM_ARC_A1) a1 = ARIM_ARC_A1;
    if (a1 <= a0) return;

    lv_area_t s;
    arim_sector_bbox(ac, rin, rout, a0, a1, &s);
    lv_area_increase(&s, half + 2, half + 2);
    lv_obj_invalidate_area(ring, &s);
}

static void arim_sweep_exec(void *var, int32_t deg)
{
    lv_obj_t *ring = (lv_obj_t *)var;
    arim_of(ring)->sweep_deg = deg;
    arim_invalidate_anim(ring);
}

static void arim_prog_exec(void *var, int32_t deg)
{
    lv_obj_t *ring = var;
    arim_t *r = arim_of(ring);
    if (!r) return;
    r->prog_deg = deg;
    arim_frame(ring);
}

void arim_sweep_start(lv_obj_t *ring)
{
    if (!ring) return;
    arim_t *r = arim_of(ring);
    arim_breath_stop(ring);

    if (r->searching) return;
    r->searching = true;

    int32_t a0 = ARIM_ARC_A0 - PAIR_SWEEP_DEG;
    int32_t a1 = ARIM_ARC_A1;
    r->sweep_deg = a0;
    r->inval_deg = a0;

    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, arim_sweep_exec);
    lv_anim_set_values(&a, a0, a1);
    lv_anim_set_duration(&a, PAIR_SWEEP_MS);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void arim_sweep_stop(lv_obj_t *ring)
{
    if (!ring) return;
    arim_t *r = arim_of(ring);
    if (!r->searching) return;
    r->searching = false;
    lv_anim_del(ring, arim_sweep_exec);
    lv_obj_invalidate(ring);
}

static void arim_breath_exec(void *var, int32_t opa)
{
    lv_obj_t *ring = (lv_obj_t *)var;
    arim_of(ring)->opa = opa;
    arim_invalidate_anim(ring);
}

static void arim_breath_stop(lv_obj_t *ring)
{
    if (!ring) return;
    arim_t *r = arim_of(ring);
    if (!r->breathing) return;
    r->breathing = false;
    lv_anim_del(ring, arim_breath_exec);
    r->opa = LV_OPA_COVER;
    lv_obj_invalidate(ring);
}

void arim_stop(lv_obj_t *ring)
{
    if (!ring) return;
    arim_t *r = arim_of(ring);
    arim_sweep_stop(ring);
    arim_breath_stop(ring);
    if (r && r->progress) {
        r->progress = false;
        lv_anim_del(ring, arim_prog_exec);
    }
}

void arim_breath_start(lv_obj_t *ring)
{
    if (!ring) return;
    arim_t *r = arim_of(ring);
    arim_sweep_stop(ring);
    if (r->breathing) return;
    r->breathing = true;
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, arim_breath_exec);
    lv_anim_set_values(&a, 70, LV_OPA_COVER);
    lv_anim_set_duration(&a, 1500);
    lv_anim_set_reverse_duration(&a, 1500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void arim_progress(lv_obj_t *ring, int32_t deg, uint32_t col, bool animate)
{
    arim_t *r = arim_of(ring);
    if (!r) return;
    arim_stop(ring);
    r->progress = true;
    if (r->col != col) {
        r->col = col;
        r->prog_deg = 0;
    }
    if (!animate || r->prog_deg == deg) {
        r->prog_deg = deg;
        arim_frame(ring);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, ring);
    lv_anim_set_exec_cb(&a, arim_prog_exec);
    lv_anim_set_values(&a, r->prog_deg, deg);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

lv_obj_t *arim_ring_create(lv_obj_t *parent, arim_t *rim)
{
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 2 * SERIN_PX(NOLINK_R_OUTER), 2 * SERIN_PX(NOLINK_R_OUTER));
    lv_obj_center(ring);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(ring, rim);
    lv_obj_add_event_cb(ring, arim_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    return ring;
}
