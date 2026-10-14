/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#include "serin_tokens.h"
#include "icons.h"

#define ACK_OPA_PILL   90
#define ACK_OPA_CHIP   64
#define ACK_OPA_TILE   20
#define ACK_OPA_UNITS  38

#define TF_GAP_PILL_ROW  SERIN_PX(16)
#define TF_GAP_DLG_ROW   SERIN_PX(14)
#define TF_CAP_PILL_ROW  (TF_GAP_PILL_ROW / 2)
#define TF_CAP_DLG_ROW   (TF_GAP_DLG_ROW / 2)
#define TF_ROW_PAD       SERIN_PX(20)
#define TF_DLG_COL_PAD   20

typedef struct {
    int32_t xcap;
    const char *name;
} touch_floor_arg_t;

lv_obj_t *mkicon(lv_obj_t *p, const serin_icon_t *ic, int disp, uint32_t col);
void      icon_set(lv_obj_t *im, const serin_icon_t *ic, uint32_t col);
void      icon_fit(lv_obj_t *im, const serin_icon_t *ic, int disp);
lv_obj_t *mklabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, const char *t);
lv_obj_t *mkbody(lv_obj_t *p, const lv_font_t *f, uint32_t c, const char *t,
                 int32_t w);
lv_obj_t *mkdot(lv_obj_t *p, int sz, uint32_t c);
lv_obj_t *mkcol(lv_obj_t *parent, int32_t w, int pad_row, int32_t tf_bottom);
lv_obj_t *mkrow(lv_obj_t *parent, int32_t pad_col);
lv_obj_t *mkpillrow(lv_obj_t *parent, int32_t gap, int32_t pad_top_extra);
lv_obj_t *mkpill(lv_obj_t *parent, const char *text, uint32_t txt_col,
                 int radius, int pad_h, int pad_v);
lv_obj_t *mkslot_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                      const char *text, int width, int design_y);
/* Return the full touch target; out_label may be NULL for a static action. */
lv_obj_t *mkslot_action(lv_obj_t *parent, const char *text, lv_event_cb_t on_click,
                       lv_obj_t **out_label);
void      press_ack(lv_obj_t *o, lv_opa_t opa);
void      ui_fade_in(lv_obj_t *obj, uint32_t ms);
void      ui_fade_out(lv_obj_t *obj, uint32_t ms, lv_anim_completed_cb_t done);
void      touch_floor(lv_obj_t *o, const touch_floor_arg_t *arg);
void      touch_floor_pair(lv_obj_t *a, touch_floor_arg_t *aa,
                           lv_obj_t *b, touch_floor_arg_t *bb, int32_t xcap);
