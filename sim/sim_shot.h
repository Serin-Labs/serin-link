// SPDX-License-Identifier: GPL-3.0-only
#pragma once
int sim_shot(const char *path);   /* active screen + lv_layer_top -> PNG; 0 = ok */
int sim_shot_fb(const char *path);/* the RETAINED rgb565 panel -> PNG. Unlike sim_shot (an
                                   * lv_snapshot, always a full re-render), this shows stale
                                   * pixels — the only way to catch a missed invalidation. */
