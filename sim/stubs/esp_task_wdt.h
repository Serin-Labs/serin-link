// SPDX-License-Identifier: GPL-3.0-only
#pragma once
/* Task-watchdog stub for the simulator.
 *
 * ui.c feeds the TWDT from an LVGL timer so that a wedged LVGL task reboots
 * the dial instead of leaving a frozen screen (see lvgl_wdt_cb). There is no
 * watchdog to feed on the host, and nothing about the UI's behaviour depends
 * on one, so both calls are no-ops here.
 *
 * esp_task_wdt_add returns 0 (= ESP_OK) so the subscribe-once path in
 * lvgl_wdt_cb takes the same branch it takes on the device, rather than the
 * "TWDT is off in this build" branch — the simulator should exercise the
 * shipping control flow, not a fallback the dial never uses.
 */
/* ui.c checks the subscribe against ESP_OK, and the simulator has no esp_err.h
 * to get it from. Guarded so this cannot collide if one ever appears. */
#ifndef ESP_OK
#define ESP_OK 0
#endif

static inline int esp_task_wdt_add(void *task_handle) { (void)task_handle; return 0; }
static inline int esp_task_wdt_reset(void) { return 0; }
