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
