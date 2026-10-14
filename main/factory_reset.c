/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "factory_reset.h"

#define RG_FIRE_MS 10000

reset_gesture_t reset_gesture_step(bool held_at_boot, bool held_now, int hold_ms)
{
    if (!held_at_boot) return RG_CANCELLED;
    if (!held_now)     return RG_CANCELLED;
    return (hold_ms >= RG_FIRE_MS) ? RG_FIRE : RG_WAITING;
}

bool reset_gesture_boot_armed(bool power_on, bool held)
{
    return power_on && held;
}

#ifndef HOST_TEST
#include "ui_theme.h"
#include "ui_fonts_dial.h"
#include "ui_widgets.h"
#include "serin_tokens.h"

static lv_obj_t *build_face(lv_obj_t **out_arc)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, SERIN_DISP, SERIN_DISP);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_radius(p, SERIN_C, 0);
    lv_obj_set_style_transform_pivot_x(p, SERIN_C, 0);
    lv_obj_set_style_transform_pivot_y(p, SERIN_C, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(COL_FACE), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *arc = lv_arc_create(p);
    int d = 2 * SERIN_PX(DIAL_R) + SERIN_PX(SERIN_TRACK_W);
    lv_obj_set_size(arc, d, d);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, SERIN_PX(SERIN_TRACK_W), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, SERIN_PX(SERIN_TRACK_W), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_ALERT_NOLINK), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);

    lv_obj_t *col = mkcol(p, SERIN_PX(360), 16, 0);
    lv_obj_center(col);
    mkbody(col, FONT_DIAL_OFFLINE, COL_ALERT_NOLINK, "Keep holding to reset", SERIN_PX(340));
    lv_obj_t *cap = mklabel(col, FONT_HINT, COL_GREY, "Release to cancel");
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);

    *out_arc = arc;
    return p;
}

void factory_reset_face_build(int pct)
{
    lv_obj_t *arc = NULL;
    build_face(&arc);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (arc) lv_arc_set_value(arc, pct);
}

#ifndef SERIN_SIM
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp.h"
#include "link_ota.h"

void factory_reset_execute(void)
{
    /* A restart now would bring the previous version back on wiped settings. */
    if (link_ota_refuse_restart()) return;
    nvs_handle_t h;
    if (nvs_open("serin",  NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    if (nvs_open("espnow", NVS_READWRITE, &h) == ESP_OK) { nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    esp_restart();
}

void factory_reset_boot_check(void)
{
    if (!reset_gesture_boot_armed(esp_reset_reason() == ESP_RST_POWERON,
                                  bsp_button_pressed())) return;
    /* Power cut on the Installed face: the first boot is a power-on and still
       soaking. Skip the hold, since the refusal needs the UI that is not up yet. */
    if (link_ota_soaking()) return;

    lv_obj_t *face = NULL, *arc = NULL;
    if (lvgl_port_lock(0)) {
        face = build_face(&arc);
        lvgl_port_unlock();
    }

    int hold_ms = 0;
    reset_gesture_t rg = RG_WAITING;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        hold_ms += 50;
        bool held_now = bsp_button_pressed();
        rg = reset_gesture_step(true, held_now, hold_ms);

        if (lvgl_port_lock(0)) {
            if (arc) {
                int pct = hold_ms * 100 / RG_FIRE_MS;
                if (pct > 100) pct = 100;
                lv_arc_set_value(arc, pct);
            }
            lvgl_port_unlock();
        }

        if (rg != RG_WAITING) break;
    }

    if (lvgl_port_lock(0)) {
        if (face) lv_obj_del(face);
        lvgl_port_unlock();
    }

    if (rg == RG_FIRE) factory_reset_execute();
}
#endif
#endif
