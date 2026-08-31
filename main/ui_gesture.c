/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_gesture.h"
#include <stdlib.h>

typedef enum { CLS_PLAIN, CLS_PENDING, CLS_SWIPE } uig_class_t;

struct uig {
    const uig_cfg_t *cfg;
    uig_class_t cls;
    bool eaten;
    bool drag_active;
    bool dragged;
    bool swiped;
    bool held;
    bool h_armed, v_armed;
    lv_point_t p0;
    uint32_t t0;
};

static void uig_pressed(lv_event_t *e)
{
    uig_t *g = lv_event_get_user_data(e);
    const uig_cfg_t *c = g->cfg;

    g->cls = CLS_PLAIN;
    g->eaten = g->drag_active = g->dragged = g->swiped = g->held = false;
    if (c->press_eats && c->press_eats()) { g->eaten = true; return; }

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (c->on_press) c->on_press();

    g->h_armed = c->swipe_min_px && c->on_swipe
                 && (!c->swipe_gate || c->swipe_gate());
    g->v_armed = c->swipe_min_px && c->on_swipe_v
                 && (!c->swipe_v_gate || c->swipe_v_gate());
    if (g->h_armed || g->v_armed) {
        g->p0  = p;
        g->t0  = lv_tick_get();
        g->cls = CLS_PENDING;
    }
    if (c->drag_arm && c->on_drag_frame)
        g->drag_active = c->drag_arm(p);
}

static void uig_pressing(lv_event_t *e)
{
    uig_t *g = lv_event_get_user_data(e);
    const uig_cfg_t *c = g->cfg;
    if (g->eaten) return;

    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (g->cls == CLS_PENDING) {
        int dx = p.x - g->p0.x, dy = p.y - g->p0.y;
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        uint32_t el = lv_tick_elaps(g->t0);
        bool h_ok = g->h_armed && ax >= c->swipe_dom * ay;
        bool v_ok = g->v_armed && ay >= c->swipe_dom * ax;
        if (h_ok && ax >= c->swipe_min_px && el <= c->swipe_window_ms) {
            g->cls    = CLS_SWIPE;
            g->swiped = true;
            c->on_swipe(dx < 0 ? +1 : -1);
        } else if (v_ok && ay >= c->swipe_min_px && el <= c->swipe_window_ms) {
            g->cls    = CLS_SWIPE;
            g->swiped = true;
            c->on_swipe_v(dy < 0 ? +1 : -1);
        } else if ((ax + ay >= c->swipe_dial_px && !h_ok && !v_ok)
                   || el > c->swipe_window_ms) {
            g->cls = CLS_PLAIN;
        }
    }
    if (g->cls == CLS_SWIPE) return;

    if (!g->drag_active) return;
    if (c->on_drag_frame(p, g->cls == CLS_PLAIN))
        g->dragged = true;
}

static void uig_long(lv_event_t *e)
{
    uig_t *g = lv_event_get_user_data(e);
    const uig_cfg_t *c = g->cfg;
    if (g->eaten) { if (c->on_eaten) c->on_eaten(); return; }
    if (g->dragged || g->swiped) return;
    if (c->on_hold && c->on_hold()) {
        g->held = true;
        g->drag_active = false;
    }
}

static void uig_released(lv_event_t *e)
{
    uig_t *g = lv_event_get_user_data(e);
    g->drag_active = false;
    if (g->cfg->on_release) g->cfg->on_release();
    if (g->dragged && g->cfg->on_drag_end) g->cfg->on_drag_end();
}

static void uig_clicked(lv_event_t *e)
{
    uig_t *g = lv_event_get_user_data(e);
    if (g->eaten && g->cfg->on_eaten) g->cfg->on_eaten();
    g->eaten = g->dragged = g->swiped = g->held = false;
    g->cls = CLS_PLAIN;
}

uig_t *uig_attach(lv_obj_t *obj, const uig_cfg_t *cfg)
{
    uig_t *g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->cfg = cfg;
    lv_obj_add_event_cb(obj, uig_pressed,  LV_EVENT_PRESSED,      g);
    lv_obj_add_event_cb(obj, uig_pressing, LV_EVENT_PRESSING,     g);
    lv_obj_add_event_cb(obj, uig_released, LV_EVENT_RELEASED,     g);
    lv_obj_add_event_cb(obj, uig_long,     LV_EVENT_LONG_PRESSED, g);
    lv_obj_add_event_cb(obj, uig_clicked,  LV_EVENT_CLICKED,      g);
    return g;
}

bool uig_click_is_tap(const uig_t *g)
{
    return !(g->eaten || g->dragged || g->swiped || g->held);
}

void uig_eat_click(uig_t *g)
{
    if (g) g->held = true;
}
