// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdbool.h>
#include <stdint.h>
extern bool sim_headless;          /* set once in main() before display start */
extern volatile bool sim_quit;
void sim_link_set(int pv, int age_s);   /* stub espnow peer_version / rx age */
bool sim_disp_asleep(void);             /* has bsp_display_sleep(true) fired without a
                                         * matching (false)? sim_cmd `info` disp_sleep= */
void sim_tap(int x, int y);             /* headless synthetic touch (sim_cmd `tap`) */
void sim_swipe(int x0, int y0, int x1, int y1, int ms); /* timed press-move-release path */
void sim_touch_hold(int x, int y);      /* press and HOLD at (x,y) — non-blocking; sim_cmd `touchhold` */
void sim_touch_up(void);                /* release a sim_touch_hold; sim_cmd `touchup` */
void sim_press_down(void);              /* knob button down + arm the 700ms hold-fires-long path */
void sim_press_up(void);                /* knob button up (no auto-click; sim_cmd `press` owns that) */
const uint16_t *sim_fb(void);           /* the retained rgb565 panel (headless; NULL otherwise) —
                                         * the INCREMENTALLY rendered pixels, stale ones included */
uint64_t sim_dirty_px_take(uint32_t *flushes);  /* pixels LVGL was asked to repaint since the last
                                                 * call, and reset. sim_cmd `dirty`. */
bool sim_reset_fired(void);             /* has the factory_reset_execute stub latched?
                                         * sim_cmd `info` reset_fired= */
