/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#include <string.h>

static inline bool ui__local(lv_obj_t *o, lv_style_prop_t p, lv_style_selector_t s,
                             lv_style_value_t *v)
{
    return lv_obj_get_local_style_prop(o, p, v, s) == LV_STYLE_RES_FOUND;
}

static inline void ui_style_num(lv_obj_t *o, lv_style_prop_t p, int32_t n, lv_style_selector_t s)
{
    lv_style_value_t v;
    if (ui__local(o, p, s, &v) && v.num == n) return;
    lv_obj_set_local_style_prop(o, p, (lv_style_value_t){ .num = n }, s);
}

static inline void ui_style_color(lv_obj_t *o, lv_style_prop_t p, lv_color_t c, lv_style_selector_t s)
{
    lv_style_value_t v;
    if (ui__local(o, p, s, &v) && lv_color_eq(v.color, c)) return;
    lv_obj_set_local_style_prop(o, p, (lv_style_value_t){ .color = c }, s);
}

static inline void ui_style_ptr(lv_obj_t *o, lv_style_prop_t p, const void *ptr, lv_style_selector_t s)
{
    lv_style_value_t v;
    if (ui__local(o, p, s, &v) && v.ptr == ptr) return;
    lv_obj_set_local_style_prop(o, p, (lv_style_value_t){ .ptr = ptr }, s);
}

static inline void ui_align(lv_obj_t *o, lv_align_t a, int32_t x, int32_t y)
{
    ui_style_num(o, LV_STYLE_ALIGN, a, 0);
    lv_obj_set_pos(o, x, y);
}

void ui_align_to(lv_obj_t *o, const lv_obj_t *base, lv_align_t a, int32_t x, int32_t y);

static inline bool ui_label_text(lv_obj_t *o, const char *t)
{
    const char *cur = lv_label_get_text(o);
    if (cur && t && strcmp(cur, t) == 0) return false;
    lv_label_set_text(o, t);
    return true;
}

bool ui_label_fmt(lv_obj_t *o, const char *fmt, ...);

static inline void ui_image_src(lv_obj_t *o, const void *src)
{
    if (lv_image_get_src(o) == src) return;
    lv_image_set_src(o, src);
}

static inline lv_value_precise_t ui__arc_norm(lv_value_precise_t a) { return a > 360 ? a - 360 : a; }
static inline void ui_arc_angles(lv_obj_t *o, lv_value_precise_t s, lv_value_precise_t e)
{
    if (lv_arc_get_angle_start(o) == ui__arc_norm(s) && lv_arc_get_angle_end(o) == ui__arc_norm(e)) return;
    lv_arc_set_angles(o, s, e);
}
static inline void ui_arc_bg_angles(lv_obj_t *o, lv_value_precise_t s, lv_value_precise_t e)
{
    if (lv_arc_get_bg_angle_start(o) == ui__arc_norm(s) && lv_arc_get_bg_angle_end(o) == ui__arc_norm(e)) return;
    lv_arc_set_bg_angles(o, s, e);
}

#define lv_obj_set_style_bg_color(o, c, s)         ui_style_color((o), LV_STYLE_BG_COLOR,          (c), (s))
#define lv_obj_set_style_text_color(o, c, s)       ui_style_color((o), LV_STYLE_TEXT_COLOR,        (c), (s))
#define lv_obj_set_style_arc_color(o, c, s)        ui_style_color((o), LV_STYLE_ARC_COLOR,         (c), (s))
#define lv_obj_set_style_border_color(o, c, s)     ui_style_color((o), LV_STYLE_BORDER_COLOR,      (c), (s))
#define lv_obj_set_style_outline_color(o, c, s)    ui_style_color((o), LV_STYLE_OUTLINE_COLOR,     (c), (s))
#define lv_obj_set_style_image_recolor(o, c, s)    ui_style_color((o), LV_STYLE_IMAGE_RECOLOR,     (c), (s))
#define lv_obj_set_style_bg_opa(o, v, s)           ui_style_num((o),   LV_STYLE_BG_OPA,            (v), (s))
#define lv_obj_set_style_text_opa(o, v, s)         ui_style_num((o),   LV_STYLE_TEXT_OPA,          (v), (s))
#define lv_obj_set_style_arc_opa(o, v, s)          ui_style_num((o),   LV_STYLE_ARC_OPA,           (v), (s))
#define lv_obj_set_style_opa(o, v, s)              ui_style_num((o),   LV_STYLE_OPA,               (v), (s))
#define lv_obj_set_style_border_opa(o, v, s)       ui_style_num((o),   LV_STYLE_BORDER_OPA,        (v), (s))
#define lv_obj_set_style_outline_opa(o, v, s)      ui_style_num((o),   LV_STYLE_OUTLINE_OPA,       (v), (s))
#define lv_obj_set_style_image_opa(o, v, s)        ui_style_num((o),   LV_STYLE_IMAGE_OPA,         (v), (s))
#define lv_obj_set_style_image_recolor_opa(o, v, s) ui_style_num((o),  LV_STYLE_IMAGE_RECOLOR_OPA, (v), (s))
#define lv_obj_set_style_radius(o, v, s)           ui_style_num((o),   LV_STYLE_RADIUS,            (v), (s))
#define lv_obj_set_style_border_width(o, v, s)     ui_style_num((o),   LV_STYLE_BORDER_WIDTH,      (v), (s))
#define lv_obj_set_style_outline_width(o, v, s)    ui_style_num((o),   LV_STYLE_OUTLINE_WIDTH,     (v), (s))
#define lv_obj_set_style_outline_pad(o, v, s)      ui_style_num((o),   LV_STYLE_OUTLINE_PAD,       (v), (s))
#define lv_obj_set_style_arc_width(o, v, s)        ui_style_num((o),   LV_STYLE_ARC_WIDTH,         (v), (s))
#define lv_obj_set_style_arc_rounded(o, v, s)      ui_style_num((o),   LV_STYLE_ARC_ROUNDED,       (v), (s))
#define lv_obj_set_style_text_align(o, v, s)       ui_style_num((o),   LV_STYLE_TEXT_ALIGN,        (v), (s))
#define lv_obj_set_style_text_letter_space(o, v, s) ui_style_num((o),  LV_STYLE_TEXT_LETTER_SPACE, (v), (s))
#define lv_obj_set_style_max_width(o, v, s)        ui_style_num((o),   LV_STYLE_MAX_WIDTH,         (v), (s))
#define lv_obj_set_style_max_height(o, v, s)       ui_style_num((o),   LV_STYLE_MAX_HEIGHT,        (v), (s))
#define lv_obj_set_style_pad_top(o, v, s)          ui_style_num((o),   LV_STYLE_PAD_TOP,           (v), (s))
#define lv_obj_set_style_pad_bottom(o, v, s)       ui_style_num((o),   LV_STYLE_PAD_BOTTOM,        (v), (s))
#define lv_obj_set_style_pad_left(o, v, s)         ui_style_num((o),   LV_STYLE_PAD_LEFT,          (v), (s))
#define lv_obj_set_style_pad_right(o, v, s)        ui_style_num((o),   LV_STYLE_PAD_RIGHT,         (v), (s))
#define lv_obj_set_style_pad_hor(o, v, s)          (ui_style_num((o), LV_STYLE_PAD_LEFT, (v), (s)), ui_style_num((o), LV_STYLE_PAD_RIGHT,  (v), (s)))
#define lv_obj_set_style_pad_ver(o, v, s)          (ui_style_num((o), LV_STYLE_PAD_TOP,  (v), (s)), ui_style_num((o), LV_STYLE_PAD_BOTTOM, (v), (s)))
#define lv_obj_set_style_pad_all(o, v, s)          (lv_obj_set_style_pad_hor((o), (v), (s)), lv_obj_set_style_pad_ver((o), (v), (s)))
#define lv_obj_set_style_pad_row(o, v, s)          ui_style_num((o),   LV_STYLE_PAD_ROW,           (v), (s))
#define lv_obj_set_style_pad_column(o, v, s)       ui_style_num((o),   LV_STYLE_PAD_COLUMN,        (v), (s))
#define lv_obj_set_style_margin_top(o, v, s)       ui_style_num((o),   LV_STYLE_MARGIN_TOP,        (v), (s))
#define lv_obj_set_style_margin_bottom(o, v, s)    ui_style_num((o),   LV_STYLE_MARGIN_BOTTOM,     (v), (s))
#define lv_obj_set_style_transform_pivot_x(o, v, s) ui_style_num((o),  LV_STYLE_TRANSFORM_PIVOT_X, (v), (s))
#define lv_obj_set_style_transform_pivot_y(o, v, s) ui_style_num((o),  LV_STYLE_TRANSFORM_PIVOT_Y, (v), (s))
#define lv_obj_set_style_anim_duration(o, v, s)    ui_style_num((o),   LV_STYLE_ANIM_DURATION,     (v), (s))
#define lv_obj_set_style_text_font(o, f, s)        ui_style_ptr((o),   LV_STYLE_TEXT_FONT,         (f), (s))
#define lv_obj_align(o, a, x, y)                   ui_align((o), (a), (x), (y))
#define lv_obj_align_to(o, b, a, x, y)             ui_align_to((o), (b), (a), (x), (y))
#define lv_label_set_text(o, t)                    ((void)ui_label_text((o), (t)))
#define lv_label_set_text_fmt(o, ...)              ((void)ui_label_fmt((o), __VA_ARGS__))
#define lv_image_set_src(o, src)                   ui_image_src((o), (src))
#define lv_arc_set_angles(o, s, e)                 ui_arc_angles((o), (s), (e))
#define lv_arc_set_bg_angles(o, s, e)              ui_arc_bg_angles((o), (s), (e))
