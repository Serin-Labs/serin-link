/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sl2_proto.h"

static inline int ui_dd_from_dc(int16_t dc, bool use_f, uint8_t ftab)
{
    return use_f ? sl2_dc_to_display(dc, true, ftab) * 10 : dc;
}

static inline int16_t ui_dd_to_dc(int dd, bool use_f, uint8_t ftab)
{
    return use_f ? sl2_display_to_dc(dd / 10, true, ftab) : (int16_t)dd;
}

/* A display value carried into the other unit. Values <= 0 are unknown or a
 * "not sent" marker and pass through unchanged. */
static inline int ui_dd_convert(int dd, bool from_f, bool to_f, uint8_t ftab)
{
    if (dd <= 0 || from_f == to_f) return dd;
    return ui_dd_from_dc(ui_dd_to_dc(dd, from_f, ftab), to_f, ftab);
}

/* One display value across unit flips. Converting there and back rounds
 * (19.5 C has no exact Fahrenheit twin), so a flip back that finds the value
 * unchanged since the last flip restores the remembered original instead. */
typedef struct { bool on, from_f, to_f; int before, after; } ui_dd_flip_t;

static inline int ui_dd_flip(ui_dd_flip_t *m, int dd, bool from_f, bool to_f, uint8_t ftab)
{
    if (from_f == to_f) return dd;
    bool back = m->on && dd == m->after && from_f == m->to_f && to_f == m->from_f;
    int v = back ? m->before : ui_dd_convert(dd, from_f, to_f, ftab);
    *m = (ui_dd_flip_t){ .on = true, .from_f = from_f, .to_f = to_f, .before = dd, .after = v };
    return v;
}
