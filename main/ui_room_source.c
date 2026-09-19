/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "sl2_proto.h"
#include <stdint.h>
#include <string.h>

static struct sl2_room_source_entry s_options[SL2_ROOM_CATALOG_MAX];
static int s_count;
static uint64_t s_selected_id;
static uint8_t s_status;
static int s_pick;
static int s_drawn = -1;
static bool s_source_removed;
static bool s_follow_pick;

static struct {
    lv_obj_t *panel, *list, *rows[SL2_ROOM_CATALOG_MAX + 1];
    lv_obj_t *names[SL2_ROOM_CATALOG_MAX + 1], *hint;
} RP;

bool room_source_can_enter(void) { return s_count > 0; }

static int option_index(uint64_t id)
{
    for (int i = 0; i < s_count; i++)
        if (s_options[i].id == id) return i;
    return -1;
}

static int selected_index(void)
{
    int i = option_index(s_selected_id);
    return i >= 0 ? i : s_count; /* Back also keeps an empty catalog safe. */
}

static void commit_pick(void)
{
    if (s_pick >= 0 && s_pick < s_count)
        notify_room_source_id(s_options[s_pick].id);
    settings_land(SET_SYSTEM);
}

static void row_click(lv_event_t *e)
{
    s_pick = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    commit_pick();
}

void build_room_source(void)
{
    lv_obj_t *p = RP.panel = s_panel[SCREEN_ROOM_SOURCE] = mkpanel(COL_FACE, false);
    lv_obj_t *title = mklabel(p, FONT_SET_NAME, COL_ITEM_GREEN, "Room Sensor");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SERIN_PX(58));

    RP.list = lv_obj_create(p);
    lv_obj_remove_style_all(RP.list);
    lv_obj_set_size(RP.list, SERIN_PX(330), SERIN_PX(290));
    lv_obj_align(RP.list, LV_ALIGN_CENTER, 0, SERIN_PX(15));
    lv_obj_set_flex_flow(RP.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(RP.list, LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(RP.list, SERIN_PX(8), 0);
    lv_obj_set_scroll_dir(RP.list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(RP.list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i <= s_count; i++) {
        lv_obj_t *row = RP.rows[i] = lv_obj_create(RP.list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, SERIN_PX(310), SERIN_TOUCH_MIN);
        lv_obj_set_style_radius(row, SERIN_TOUCH_MIN / 2, 0);
        lv_obj_set_style_bg_opa(row, OPA_FILL_NEUTRAL, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(COL_WHITE), 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, row_click, LV_EVENT_CLICKED, NULL);
        press_ack(row, ACK_OPA_CHIP);
        char nm[SL2_ROOM_SOURCE_NAME_LEN + 1];
        if (i < s_count) {
            memcpy(nm, s_options[i].name, SL2_ROOM_SOURCE_NAME_LEN);
            nm[SL2_ROOM_SOURCE_NAME_LEN] = '\0';
        }
        RP.names[i] = mklabel(row, FONT_DIAL_PILL, COL_WHITE,
                              i < s_count ? nm : "Back");
        lv_obj_set_width(RP.names[i], SERIN_PX(286));
        /* Size to the wrapped text, capped at two lines: a fixed two-line box
         * would leave a one-line name drawn at its top edge, not centred. */
        lv_obj_set_height(RP.names[i], LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(RP.names[i],
                                    2 * lv_font_get_line_height(FONT_DIAL_PILL), 0);
        lv_label_set_long_mode(RP.names[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(RP.names[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(RP.names[i]);
    }

    RP.hint = mklabel(p, FONT_HINT, COL_GREY, H_PRESS("choose"));
    lv_obj_align(RP.hint, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(42));
    s_drawn = -1;
    s_follow_pick = true;   /* a fresh list starts at the top, not at the pick */
}

void room_source_destroy(void)
{
    memset(&RP, 0, sizeof RP);
    s_drawn = -1;
}

void room_source_enter(void)
{
    s_pick = selected_index();
    s_source_removed = false;
    s_drawn = -1;
    s_follow_pick = true;
}

static void paint_row(int i)
{
    if (i < 0 || i > s_count || !RP.rows[i] || !RP.names[i]) return;
    bool active  = i == s_pick;
    bool applied = i < s_count && s_options[i].id == s_selected_id;
    uint32_t bg  = !active ? COL_WHITE : (i == s_count ? COL_BACK : COL_ITEM_GREEN);
    lv_obj_set_style_bg_opa(RP.rows[i], active ? LV_OPA_COVER : OPA_FILL_NEUTRAL, 0);
    lv_obj_set_style_bg_color(RP.rows[i], lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(RP.names[i],
        lv_color_hex(active ? COL_ON_LIGHT : applied ? COL_GREEN : COL_WHITE), 0);
    lv_obj_set_height(RP.names[i], active
                      ? lv_font_get_line_height(FONT_DIAL_PILL)
                      : LV_SIZE_CONTENT);
    lv_label_set_long_mode(RP.names[i], active
                           ? LV_LABEL_LONG_SCROLL
                           : LV_LABEL_LONG_WRAP);
    lv_obj_center(RP.names[i]);
}

void refresh_room_source(void)
{
    if (!RP.panel) return;
    if (s_pick < 0 || s_pick > s_count) s_pick = selected_index();
    if (s_drawn < 0) {
        for (int i = 0; i <= s_count; i++) paint_row(i);
    } else if (s_drawn != s_pick) {
        paint_row(s_drawn);
        paint_row(s_pick);
    }
    s_drawn = s_pick;
    /* Follow the pick only when something moved it.  Every telemetry push
     * refreshes the current screen, so scrolling here unconditionally would
     * drag a finger-scrolled list back to the picked row a second later. */
    if (s_follow_pick) {
        s_follow_pick = false;
        if (RP.rows[s_pick]) lv_obj_scroll_to_view(RP.rows[s_pick], LV_ANIM_OFF);
    }
    lv_label_set_text(RP.hint,
        s_count == 0                                          ? "No sensors available" :
        s_pick == s_count && s_source_removed                 ? "Sensor no longer available" :
        s_pick == s_count                                     ? H_PRESS("return") :
        (s_options[s_pick].id == s_selected_id &&
         s_status == SL2_ROOMST_OK)                           ? "Selected"
                                                              : H_PRESS("choose"));
}

static void pick_set(int v)
{
    s_pick = v < 0 ? 0 : v > s_count ? s_count : v;
    s_source_removed = false;
    s_follow_pick = true;
    refresh_room_source();
}

void room_source_turn(int delta)
{
    if (!delta) return;
    pick_set(s_pick + (delta > 0 ? 1 : -1));
}

void room_source_press(void) { commit_pick(); }

void ui_set_room_sources(const struct sl2_room_source_entry *entries, int count,
                         uint64_t selected_id, uint8_t status)
{
    if (count < 0) count = 0;
    if (count > SL2_ROOM_CATALOG_MAX) count = SL2_ROOM_CATALOG_MAX;
    bool same = count == s_count &&
                (count == 0 || memcmp(s_options, entries,
                                      (size_t)count * sizeof s_options[0]) == 0);
    bool visible = s_cur == SCREEN_ROOM_SOURCE;
    bool had_source = visible && s_pick >= 0 && s_pick < s_count;
    bool had_back = visible && s_pick == s_count;
    uint64_t focused_id = had_source ? s_options[s_pick].id : 0;
    if (count) memcpy(s_options, entries, (size_t)count * sizeof s_options[0]);
    s_count = count;
    s_selected_id = selected_id;
    s_status = status;
    if (!same && visible) {
        int i = had_source ? option_index(focused_id) : -1;
        if (i >= 0) {
            s_pick = i;
            s_source_removed = false;
        } else if (had_source) {
            s_pick = s_count;
            s_source_removed = s_count > 0;
        } else if (had_back) {
            s_pick = s_count;
            s_source_removed = false;
        } else {
            s_pick = selected_index();
            s_source_removed = false;
        }
    }
    if (!s_panel[SCREEN_ROOM_SOURCE]) return;
    if (same) { s_drawn = -1; refresh_room_source(); }
    else      screen_rebuild(SCREEN_ROOM_SOURCE);
}

void ui_set_room_source_id(uint64_t selected_id, uint8_t status)
{
    if (selected_id == s_selected_id && status == s_status) return;
    s_selected_id = selected_id;
    s_status = status;
    if (s_cur == SCREEN_ROOM_SOURCE) { s_drawn = -1; refresh_room_source(); }
}
