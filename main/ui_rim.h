/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#include "icons.h"

#define RIM_MAX 8

typedef struct {
    int       n;
    float     ang[RIM_MAX];
    uint32_t  col[RIM_MAX];
    lv_obj_t *seg[RIM_MAX];
    lv_obj_t *node[RIM_MAX];
    lv_obj_t *glyph[RIM_MAX];
    lv_obj_t *ind;
    uint8_t   px_idle[RIM_MAX];
    uint8_t   px_sel[RIM_MAX];
    int       sel;
} rim_t;

typedef struct {
    int        n;
    const float    *ang;
    const uint32_t *col;
    const serin_icon_t *const *glyph;
    float      half_deg;
    int        glyph_r;
    int        disp_idle, disp_sel;
    const uint8_t *px_idle, *px_sel;
    int        node_px;
    lv_event_cb_t on_tap;
    bool       no_ind;
} rim_cfg_t;

#define RIM_BACK_ANG 270.0f
float     rim_stop_angle(int pos, int n, int back_pos);

lv_obj_t *rim_arc(lv_obj_t *p, float ang, float half, int w, uint32_t col);
void      rim_build(rim_t *r, lv_obj_t *parent, const rim_cfg_t *c);
void      rim_select(rim_t *r, int idx, uint32_t ind_col);
void      rim_hide(rim_t *r, bool hide);
void      rim_set_span(lv_obj_t *arc, float a0_deg, float a1_deg);
void      rim_tint(rim_t *r, int i, uint32_t col);
