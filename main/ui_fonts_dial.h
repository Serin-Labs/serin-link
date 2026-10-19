/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"

/* Name faces (18/27/40): ASCII plus U+00A0–00AC, U+00AE–0148,
 * U+014A–017E. Latin excludes soft hyphen, ŉ, and ſ. The _latin aliases
 * share glyph tables and reserve full accent height; ASCII keeps its metrics.
 * Other face-specific subsets are documented in fonts/README_fonts.md. */
LV_FONT_DECLARE(jakarta_600_18_latin);
LV_FONT_DECLARE(jakarta_600_27_latin);
LV_FONT_DECLARE(jakarta_600_40_latin);

LV_FONT_DECLARE(jakarta_600_58);
LV_FONT_DECLARE(jakarta_600_40);
LV_FONT_DECLARE(jakarta_600_27);
LV_FONT_DECLARE(jakarta_600_22);
LV_FONT_DECLARE(jakarta_600_18);
LV_FONT_DECLARE(jakarta_600_15);
LV_FONT_DECLARE(jakarta_800_15);

LV_FONT_DECLARE(jakarta_600_230);
LV_FONT_DECLARE(jakarta_600_128);
LV_FONT_DECLARE(jakarta_500_56);
LV_FONT_DECLARE(jakarta_600_33);

#define FONT_DIAL_HERO    (&jakarta_600_230)
#define FONT_DIAL_DEG     (&jakarta_500_56)
#define FONT_BAND_HERO    (&jakarta_600_128)
#define FONT_BAND_HALF    (&jakarta_600_27)
#define FONT_DIAL_MODE    (&jakarta_600_40)
#define FONT_DIAL_PILL    (&jakarta_600_27)
#define FONT_DIAL_NOW     (&jakarta_600_22)
#define FONT_DIAL_CHIP    (&jakarta_800_15)
#define FONT_CHIP_BATT    (&jakarta_600_22)
#define FONT_DIAL_OFFLINE (&jakarta_600_40)

#define FONT_SSID         (&jakarta_600_58)
#define FONT_MODE_NAME    (&jakarta_600_40)
#define FONT_HK_CODE      (&jakarta_600_40)
#define FONT_SET_NAME     (&jakarta_600_40)
#define FONT_BRIGHT       (&jakarta_600_33)
#define FONT_UNITS        (&jakarta_600_27)
#define FONT_STATUS       (&jakarta_600_18)
#define FONT_STATUS_SM    (&jakarta_600_15)
#define FONT_HINT         (&jakarta_600_15)
#define FONT_WZ_BODY      (&jakarta_600_22)
#define FONT_WZ_HINT      (&jakarta_600_18)
