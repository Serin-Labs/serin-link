/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#include <stdbool.h>

typedef struct uig uig_t;

typedef struct {
    int      swipe_min_px;
    int      swipe_dial_px;
    int      swipe_dom;
    uint32_t swipe_window_ms;
    bool (*swipe_gate)(void);
    void (*on_swipe)(int dir);
    bool (*swipe_v_gate)(void);
    void (*on_swipe_v)(int dir);

    bool (*drag_arm)(lv_point_t p);
    bool (*on_drag_frame)(lv_point_t p, bool may_engage);
    void (*on_drag_end)(void);

    bool (*on_hold)(void);

    bool (*press_eats)(void);
    void (*on_eaten)(void);

    void (*on_press)(void);
    void (*on_release)(void);
} uig_cfg_t;

uig_t *uig_attach(lv_obj_t *obj, const uig_cfg_t *cfg);

bool uig_click_is_tap(const uig_t *g);

void uig_eat_click(uig_t *g);
