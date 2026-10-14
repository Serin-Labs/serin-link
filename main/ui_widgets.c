/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_widgets.h"
#include "ui_theme.h"
#include "ui_fonts_dial.h"
#include "serin_tokens.h"
#include "ui_setif.h"
#include "src/core/lv_obj_event_private.h"
#include <stdio.h>

void icon_fit(lv_obj_t *im, const serin_icon_t *ic, int disp)
{
    if (!ic || !ic->n || disp <= 0) { LV_ASSERT(0); return; }
    const lv_image_dsc_t *d = ic->dsc[ic->n - 1];
    for (uint8_t i = 0; i < ic->n; i++)
        if ((int)ic->dsc[i]->header.w >= disp) { d = ic->dsc[i]; break; }
    lv_obj_set_user_data(im, (void *)(uintptr_t)ic);
    lv_image_set_src(im, d);
    int scale = 256 * disp / (int)d->header.w;
    if (scale >= 243 && scale <= 256) {
        lv_obj_set_size(im, d->header.w, d->header.h);
        scale = 256;
    } else {
        lv_obj_set_size(im, disp, disp);
    }
    lv_image_set_scale(im, scale);
    if (scale < 243)
        printf("icon-guard: downscale %d/256 (disp %d, variant %d)\n",
               scale, disp, (int)d->header.w);
}

lv_obj_t *mkicon(lv_obj_t *p, const serin_icon_t *ic, int disp, uint32_t col)
{
    lv_obj_t *im = lv_image_create(p);
    lv_image_set_inner_align(im, LV_IMAGE_ALIGN_CENTER);
    icon_fit(im, ic, disp);
    lv_obj_set_style_image_recolor(im, lv_color_hex(col), 0);
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_remove_flag(im, LV_OBJ_FLAG_CLICKABLE);
    return im;
}
void icon_set(lv_obj_t *im, const serin_icon_t *ic, uint32_t col)
{
    icon_fit(im, ic, lv_obj_get_style_width(im, LV_PART_MAIN));
    lv_obj_set_style_image_recolor(im, lv_color_hex(col), 0);
}

lv_obj_t *mklabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    if (t) lv_label_set_text(l, t);
    return l;
}

lv_obj_t *mkbody(lv_obj_t *p, const lv_font_t *f, uint32_t c, const char *t, int32_t w)
{
    lv_obj_t *l = mklabel(p, f, c, t);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}
lv_obj_t *mkdot(lv_obj_t *p, int sz, uint32_t c)
{
    lv_obj_t *d = lv_obj_create(p);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, sz, sz);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(c), 0);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    return d;
}

void press_ack(lv_obj_t *o, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(o, lv_color_white(), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(o, opa, LV_PART_MAIN | LV_STATE_PRESSED);
}

static int32_t touch_floor_need(lv_obj_t *o)
{
    int32_t w = lv_obj_get_width(o), h = lv_obj_get_height(o);
    int32_t need = 0;
    if (h < SERIN_TOUCH_MIN) need = (SERIN_TOUCH_MIN - h + 1) / 2;
    if (w < SERIN_TOUCH_MIN) {
        int32_t n = (SERIN_TOUCH_MIN - w + 1) / 2;
        if (n > need) need = n;
    }
    return need;
}

static void touch_floor_hit_cb(lv_event_t *e)
{
    const touch_floor_arg_t *a = lv_event_get_user_data(e);
    if (a->xcap <= 0) return;
    lv_hit_test_info_t *hit = lv_event_get_hit_test_info(e);
    lv_area_t c;
    lv_obj_get_coords(lv_event_get_current_target(e), &c);
    if (hit->point->x < c.x1 - a->xcap || hit->point->x > c.x2 + a->xcap)
        hit->res = false;
}

static int32_t touch_floor_set(lv_obj_t *o, int32_t xcap)
{
    int32_t need = touch_floor_need(o);
    lv_obj_set_ext_click_area(o, need);
    if (xcap > 0 && need > xcap) lv_obj_add_flag(o, LV_OBJ_FLAG_ADV_HITTEST);
    return need;
}

static void touch_floor_check_clip(lv_obj_t *o, int32_t need,
                                   const touch_floor_arg_t *a, bool deep)
{
    if (need <= 0) return;
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;

    int32_t ex = (a->xcap > 0 && need > a->xcap) ? a->xcap : need;
    lv_area_t oc;
    lv_obj_get_coords(o, &oc);
    lv_area_increase(&oc, ex, need);

    int32_t clip = 0, depth = 0, at = 0;
    for (lv_obj_t *p = lv_obj_get_parent(o); p; p = lv_obj_get_parent(p)) {
        lv_area_t pc;
        lv_obj_get_coords(p, &pc);
        depth++;
        int32_t c = 0;
        if (pc.x1 - oc.x1 > c) c = pc.x1 - oc.x1;
        if (pc.y1 - oc.y1 > c) c = pc.y1 - oc.y1;
        if (oc.x2 - pc.x2 > c) c = oc.x2 - pc.x2;
        if (oc.y2 - pc.y2 > c) c = oc.y2 - pc.y2;
        if (c > clip) { clip = c; at = depth; }
        if (!deep) break;
    }
    if (clip > 0) {
        LV_UNUSED(at);
        LV_LOG_WARN("touch_floor: %s -- %dpx of the %dpx growth is clipped "
                    "by the box of ancestor #%d; pad that ancestor",
                    a->name ? a->name : "?", (int)clip, (int)need, (int)at);
    }
}
static void touch_floor_cb(lv_event_t *e)
{
    const touch_floor_arg_t *a = lv_event_get_user_data(e);
    touch_floor_set(lv_event_get_current_target(e), a->xcap);
}
void touch_floor(lv_obj_t *o, const touch_floor_arg_t *arg)
{
    lv_obj_add_event_cb(o, touch_floor_cb, LV_EVENT_SIZE_CHANGED, (void *)arg);
    lv_obj_add_event_cb(o, touch_floor_hit_cb, LV_EVENT_HIT_TEST, (void *)arg);
    lv_obj_update_layout(o);
    int32_t need = touch_floor_set(o, arg->xcap);
    touch_floor_check_clip(o, need, arg, false);
}

void touch_floor_pair(lv_obj_t *a, touch_floor_arg_t *aa,
                             lv_obj_t *b, touch_floor_arg_t *ba, int32_t xcap)
{
    bool both = !lv_obj_has_flag(a, LV_OBJ_FLAG_HIDDEN) &&
                !lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN);
    aa->xcap = ba->xcap = both ? xcap : 0;
    lv_obj_update_layout(a);
    int32_t na = touch_floor_set(a, aa->xcap);
    int32_t nb = touch_floor_set(b, ba->xcap);
    touch_floor_check_clip(a, na, aa, true);
    touch_floor_check_clip(b, nb, ba, true);
}

lv_obj_t *mkcol(lv_obj_t *parent, int32_t w, int pad_row, int32_t tf_bottom)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, SERIN_PX(pad_row), 0);
    lv_obj_set_style_pad_bottom(c, tf_bottom, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    return c;
}

lv_obj_t *mkrow(lv_obj_t *parent, int32_t pad_col)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, pad_col, 0);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    return r;
}

lv_obj_t *mkpillrow(lv_obj_t *parent, int32_t gap, int32_t pad_top_extra)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    lv_obj_set_style_pad_top(row, pad_top_extra + TF_ROW_PAD, 0);
    lv_obj_set_style_pad_hor(row, TF_ROW_PAD, 0);
    lv_obj_set_style_pad_bottom(row, TF_ROW_PAD, 0);
    lv_obj_set_style_margin_top(row, -TF_ROW_PAD, 0);
    lv_obj_set_style_margin_bottom(row, -TF_ROW_PAD, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    return row;
}

lv_obj_t *mkpill(lv_obj_t *parent, const char *text, uint32_t txt_col,
                        int radius, int pad_h, int pad_v)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, SERIN_PX(radius), 0);
    lv_obj_set_style_pad_hor(b, SERIN_PX(pad_h), 0);
    lv_obj_set_style_pad_ver(b, SERIN_PX(pad_v), 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    mklabel(b, FONT_DIAL_PILL, txt_col, text);
    return b;
}

static void slot_place(lv_obj_t *obj, int design_y)
{
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, SERIN_PX(design_y) - SERIN_C);
}

lv_obj_t *mkslot_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                      const char *text, int width, int design_y)
{
    lv_obj_t *label = mklabel(parent, font, color, text);
    lv_obj_set_size(label, SERIN_PX(width), lv_font_get_line_height(font));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
    slot_place(label, design_y);
    return label;
}

static void slot_action_press_cb(lv_event_t *e)
{
    lv_obj_t *pill = lv_event_get_user_data(e);
    lv_obj_set_state(pill, LV_STATE_PRESSED, lv_event_get_code(e) == LV_EVENT_PRESSED);
}

lv_obj_t *mkslot_action(lv_obj_t *parent, const char *text, lv_event_cb_t on_click,
                       lv_obj_t **out_label)
{
    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    int height = SERIN_PX(SERIN_SLOT_CONTROL_H);
    if (height < SERIN_TOUCH_MIN) height = SERIN_TOUCH_MIN;
    lv_obj_set_size(hit, SERIN_PX(SERIN_SLOT_ACTION_W), height);
    lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    slot_place(hit, SERIN_SLOT_CONTROL_Y);

    lv_obj_t *pill = mkrow(hit, 0);
    lv_obj_set_height(pill, lv_font_get_line_height(FONT_DIAL_PILL) + 2 * SERIN_PX(11));
    lv_obj_set_style_max_width(pill, SERIN_PX(SERIN_SLOT_ACTION_W), 0);
    lv_obj_set_style_pad_hor(pill, SERIN_PX(16), 0);
    lv_obj_set_style_radius(pill, SERIN_PX(33), 0);
    lv_obj_set_style_bg_color(pill, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pill, (lv_opa_t)26, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(pill);
    lv_obj_t *label = mklabel(pill, FONT_DIAL_PILL, COL_WHITE, text);
    lv_obj_set_style_max_width(label, SERIN_PX(SERIN_SLOT_ACTION_W - 32), 0);
    lv_obj_set_height(label, lv_font_get_line_height(FONT_DIAL_PILL));
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
    if (out_label) *out_label = label;
    press_ack(pill, ACK_OPA_CHIP);
    lv_obj_add_event_cb(hit, slot_action_press_cb, LV_EVENT_PRESSED, pill);
    lv_obj_add_event_cb(hit, slot_action_press_cb, LV_EVENT_RELEASED, pill);
    lv_obj_add_event_cb(hit, slot_action_press_cb, LV_EVENT_PRESS_LOST, pill);
    lv_obj_add_event_cb(hit, on_click, LV_EVENT_CLICKED, NULL);
    return hit;
}

#include <stdarg.h>
#include <stdio.h>

void ui_align_to(lv_obj_t *o, const lv_obj_t *base, lv_align_t a, int32_t x, int32_t y)
{
    lv_style_value_t v, x0 = { .num = INT32_MIN }, y0 = { .num = INT32_MIN };
    bool tl = ui__local(o, LV_STYLE_ALIGN, 0, &v) && v.num == LV_ALIGN_TOP_LEFT;
    if (!tl) { (lv_obj_align_to)(o, base, a, x, y); return; }
    ui__local(o, LV_STYLE_X, 0, &x0);
    ui__local(o, LV_STYLE_Y, 0, &y0);
    lv_obj_enable_style_refresh(false);
    (lv_obj_align_to)(o, base, a, x, y);
    lv_obj_enable_style_refresh(true);
    lv_style_value_t x1, y1;
    bool moved = !ui__local(o, LV_STYLE_X, 0, &x1) || x1.num != x0.num ||
                 !ui__local(o, LV_STYLE_Y, 0, &y1) || y1.num != y0.num;
    if (moved) lv_obj_refresh_style(o, LV_PART_MAIN, LV_STYLE_X);
}

bool ui_label_fmt(lv_obj_t *o, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof buf) {
        va_start(ap, fmt);
        lv_label_set_text_vfmt(o, fmt, ap);
        va_end(ap);
        return true;
    }
    return ui_label_text(o, buf);
}

static void fade_exec(void *var, int32_t v) { lv_obj_set_style_opa(var, (lv_opa_t)v, 0); }
void ui_fade_in(lv_obj_t *obj, uint32_t ms)
{
    lv_anim_delete(obj, fade_exec);
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, fade_exec);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER); lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}
void ui_fade_out(lv_obj_t *obj, uint32_t ms, lv_anim_completed_cb_t done)
{
    lv_anim_delete(obj, fade_exec);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, obj); lv_anim_set_exec_cb(&a, fade_exec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP); lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}
