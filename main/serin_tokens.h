/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#ifndef SERIN_TOKENS_H
#define SERIN_TOKENS_H

#include "lvgl.h"
#include <math.h>

#include "serin_board.h"

#ifndef SERIN_DISP
#error "SERIN_DISP is not defined (serin_board.h must set it from the Kconfig board choice). It must never fall back to a default: an undefined SERIN_DISP evaluates to 0 in #if and would silently select the wrong SERIN_TOUCH_MIN."
#endif
#if SERIN_DISP != 466 && SERIN_DISP != 480
#error "SERIN_DISP must be 466 or 480 — SERIN_PPMM (the touch floor) is resolved per panel and has no meaning for any other value."
#endif

#define SERIN_S        ((float)(SERIN_DISP) / 480.0f)
#define SERIN_C        ((SERIN_DISP) / 2)
#define SERIN_PX(x)    ((lv_coord_t)((x) * SERIN_S + 0.5f))

#define SERIN_SEG_IDLE_OPA  ((lv_opa_t)66)

#define SERIN_TRACK_W       14
#define DIAL_R              224
#define SERIN_DOT_ROOM_R    9
#define SERIN_DOT_SET_R     13
#define SERIN_DOT_KEYLINE   2
#define SERIN_RIM_R         234
#define SERIN_GLYPH_R       162
#define SERIN_GLYPH_R_SET   186
#define SERIN_RIM_W_SEL     6
#define SERIN_RIM_W_IDLE    3
#define UI_FADE_MS          400
#define UI_FADE_OUT_MS      100
#define UI_DIP_OUT_MS       60
#define UI_DIP_IN_MS        200
#define SERIN_IND_HALF_DEG  8.5f

/* Shared dial slots in the 480 px design canvas. */
#define SERIN_SLOT_TITLE_Y    197
#define SERIN_SLOT_DETAIL_Y   233
#define SERIN_SLOT_CONTROL_Y  276
#define SERIN_SLOT_HINT_TOP   316
#define SERIN_SLOT_CONTROL_W  230
#define SERIN_SLOT_CONTROL_H   88
/* Narrower action targets clear every selector ring density (2–8 stops). */
#define SERIN_SLOT_ACTION_W   200

#if SERIN_DISP == 466
#define SERIN_PPMM  12.23f
#else
#define SERIN_PPMM   9.00f
#endif
#define SERIN_MM(x)      ((lv_coord_t)((x) * SERIN_PPMM + 0.5f))
#define SERIN_TOUCH_MIN  SERIN_MM(7)

#define SERIN_ARC_START     225.0f
#define SERIN_ARC_SWEEP     270.0f
#define SERIN_DEG           (3.14159265f / 180.0f)

static inline float serin_temp_t(float temp, int tmin, int tmax) {
    float t = (temp - tmin) / (float)(tmax - tmin);
    return t < 0 ? 0 : (t > 1 ? 1 : t);
}
static inline float serin_angle(float t)        { return SERIN_ARC_START + SERIN_ARC_SWEEP * t; }
static inline int   serin_lv_angle(float deg)   { return (int)(deg - 90.0f); }
static inline lv_coord_t serin_px_x(float a, int r){ return (lv_coord_t)(SERIN_C + SERIN_PX(r) * sinf(a * SERIN_DEG)); }
static inline lv_coord_t serin_px_y(float a, int r){ return (lv_coord_t)(SERIN_C - SERIN_PX(r) * cosf(a * SERIN_DEG)); }

#endif
