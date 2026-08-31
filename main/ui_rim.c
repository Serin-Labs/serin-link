/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_rim.h"
#include "ui_widgets.h"
#include "ui_setif.h"
#include "serin_tokens.h"

float rim_stop_angle(int pos, int n, int back_pos)
{
    float pitch = 360.0f / (float)n;
    float base  = (back_pos >= 0) ? (RIM_BACK_ANG - back_pos * pitch) : 0.0f;
    return base + pos * pitch;
}

void rim_set_span(lv_obj_t *arc, float a0_deg, float a1_deg)
{
    int s = ((serin_lv_angle(a0_deg) % 360) + 360) % 360;
    int e = ((serin_lv_angle(a1_deg) % 360) + 360) % 360;
    lv_arc_set_bg_angles(arc, s, e);
}

void rim_tint(rim_t *r, int i, uint32_t col)
{
    lv_obj_set_style_arc_color(r->seg[i], lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_image_recolor(r->glyph[i], lv_color_hex(col), 0);
}

lv_obj_t *rim_arc(lv_obj_t *p, float ang, float half, int w, uint32_t col)
{
    lv_obj_t *a = lv_arc_create(p);
    int sz = 2 * SERIN_PX(SERIN_RIM_R) + w;
    lv_obj_set_size(a, sz, sz);
    lv_obj_center(a);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    rim_set_span(a, ang - half, ang + half);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, lv_color_hex(col), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    return a;
}

static void glyph_resize(lv_obj_t *im, int disp_px)
{
    icon_fit(im, lv_obj_get_user_data(im), disp_px);
}

void rim_build(rim_t *r, lv_obj_t *parent, const rim_cfg_t *c)
{
    r->n = c->n;
    r->sel = 0;
    for (int i = 0; i < c->n; i++) {
        int pi = c->px_idle && c->px_idle[i] ? c->px_idle[i] : c->disp_idle;
        int ps = c->px_sel  && c->px_sel[i]  ? c->px_sel[i]  : c->disp_sel;
        r->px_idle[i] = (uint8_t)pi; r->px_sel[i] = (uint8_t)ps;
    }
    for (int i = 0; i < c->n; i++) {
        r->ang[i] = c->ang[i];
        r->col[i] = c->col[i];
        r->seg[i] = rim_arc(parent, c->ang[i], c->half_deg,
                            SERIN_PX(SERIN_RIM_W_IDLE), c->col[i]);
    }
    r->ind = c->no_ind ? NULL
           : rim_arc(parent, c->ang[0], SERIN_IND_HALF_DEG,
                     SERIN_PX(SERIN_RIM_W_SEL), c->col[0]);
    for (int i = 0; i < c->n; i++) {
        int np = SERIN_PX(c->node_px);
        lv_obj_t *nd = r->node[i] = lv_obj_create(parent);
        lv_obj_remove_style_all(nd);
        lv_obj_set_size(nd, np, np);
        lv_obj_set_pos(nd, serin_px_x(c->ang[i], c->glyph_r) - np / 2,
                            serin_px_y(c->ang[i], c->glyph_r) - np / 2);
        lv_obj_set_flex_flow(nd, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(nd, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(nd, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(nd, (void *)(intptr_t)i);
        if (c->on_tap) lv_obj_add_event_cb(nd, c->on_tap, LV_EVENT_CLICKED, NULL);
        r->glyph[i] = mkicon(nd, c->glyph[i], SERIN_PX(r->px_idle[i]), c->col[i]);
    }
}

void rim_select(rim_t *r, int idx, uint32_t ind_col)
{
    if (idx < 0) idx = 0;
    if (idx >= r->n) idx = r->n - 1;
    r->sel = idx;
    for (int i = 0; i < r->n; i++) {
        bool sel = (i == idx);
        lv_obj_set_style_arc_width(r->seg[i],
            SERIN_PX(sel ? SERIN_RIM_W_SEL : SERIN_RIM_W_IDLE), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(r->seg[i],
            sel ? LV_OPA_COVER : SERIN_SEG_IDLE_OPA, LV_PART_MAIN);
        glyph_resize(r->glyph[i], SERIN_PX(sel ? r->px_sel[i] : r->px_idle[i]));
        lv_obj_set_style_opa(r->glyph[i], sel ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    if (r->ind) {
        float a = r->ang[idx];
        rim_set_span(r->ind, a - SERIN_IND_HALF_DEG, a + SERIN_IND_HALF_DEG);
        lv_obj_set_style_arc_color(r->ind, lv_color_hex(ind_col), LV_PART_MAIN);
    }
}

void rim_hide(rim_t *r, bool hide)
{
    void (*fn)(lv_obj_t *, lv_obj_flag_t) = hide ? lv_obj_add_flag : lv_obj_remove_flag;
    for (int i = 0; i < r->n; i++) { fn(r->seg[i], LV_OBJ_FLAG_HIDDEN); fn(r->node[i], LV_OBJ_FLAG_HIDDEN); }
}
