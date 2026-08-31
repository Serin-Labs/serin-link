/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
#include "serin_tokens.h"

#define NOLINK_R_OUTER    240
#define PAIR_SWEEP_DEG     70
#define PAIR_SWEEP_MS    3000

#define ARIM_ARC_A0     135
#define ARIM_ARC_A1     (45 + 360)
#define ARIM_ARC_R      DIAL_R
#define ARIM_ARC_W      SERIN_TRACK_W
#define ARIM_SPAN_DEG   (ARIM_ARC_A1 - ARIM_ARC_A0)

#define ARIM_ARC_R_OUT  ((2 * SERIN_PX(ARIM_ARC_R) + SERIN_PX(ARIM_ARC_W)) / 2)

#define ARIM_BAND_R_IN  (ARIM_ARC_R - ARIM_ARC_W)

typedef struct {
    uint32_t    col;
    bool        searching;
    bool        breathing;
    int32_t     sweep_deg;
    int32_t     opa;
    int32_t     inval_deg;
    bool        progress;
    int32_t     prog_deg;
} arim_t;

extern arim_t s_arim_home;
extern arim_t s_arim_dlg;

lv_obj_t *arim_ring_create(lv_obj_t *parent, arim_t *rim);
void      arim_sweep_start(lv_obj_t *ring);
void      arim_breath_start(lv_obj_t *ring);
void      arim_stop(lv_obj_t *ring);
void arim_progress(lv_obj_t *ring, int32_t deg, uint32_t col, bool animate);
