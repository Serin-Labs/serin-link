/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
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
/* Keep the same touch geometry as Settings, even for a full catalog. */
#define SOURCES_PER_RING (RIM_MAX - 1)
static rim_t RR;

static struct {
    lv_obj_t *panel, *name, *hint, *position, *action;
    int first, count;
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

static const serin_icon_t *source_icon(const struct sl2_room_source_entry *source)
{
    switch (source->kind) {
    case SL2_ROOM_KIND_INTERNAL: return &room_heatpump;
    case SL2_ROOM_KIND_LINK:     return &room_link;
    case SL2_ROOM_KIND_AUTO:
        return (source->id >> 56) == SL2_ROOM_SOURCE_NS_LINK_AUTO
               ? &room_link : &room_sensor;
    default:                    return &room_sensor;
    }
}

static int ring_first(void)
{
    int pick = s_pick < s_count ? s_pick : s_count - 1;
    return pick < 0 ? 0 : pick / SOURCES_PER_RING * SOURCES_PER_RING;
}

static void source_click(lv_event_t *e)
{
    int pos = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_pick = pos < RP.count ? RP.first + pos : s_count;
    commit_pick();
}

static void source_action_click(lv_event_t *e)
{
    (void)e;
    commit_pick();
}

void build_room_source(void)
{
    lv_obj_t *p = RP.panel = s_panel[SCREEN_ROOM_SOURCE] = mkpanel(COL_FACE, false);
    RP.first = ring_first();
    RP.count = s_count - RP.first;
    if (RP.count > SOURCES_PER_RING) RP.count = SOURCES_PER_RING;
    int n = RP.count + 1;
    float ang[RIM_MAX];
    uint32_t col[RIM_MAX];
    const serin_icon_t *glyph[RIM_MAX];
    for (int i = 0; i < n; i++) {
        bool back = i == RP.count;
        ang[i] = rim_stop_angle(i, n, RP.count);
        col[i] = back ? COL_BACK : COL_ITEM_GREEN;
        glyph[i] = back ? &nav_back : source_icon(&s_options[RP.first + i]);
    }
    rim_cfg_t cfg = {
        .n = n, .ang = ang, .col = col, .glyph = glyph,
        .half_deg = 180.0f / n - 8.0f,
        .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 45, .disp_sel = 60, .node_px = 100,
        .on_tap = source_click, .no_ind = true,
    };
    rim_build(&RR, p, &cfg);

    RP.name = mkslot_label(p, FONT_SET_NAME, COL_ITEM_GREEN, "", 310, SERIN_SLOT_TITLE_Y);
    RP.position = mkslot_label(p, FONT_STATUS, COL_DIM, "", 260, SERIN_SLOT_DETAIL_Y);
    mkslot_action(p, "Choose", source_action_click, &RP.action);
    RP.hint = mkbody(p, FONT_HINT, COL_GREY, "", SERIN_PX(280));
    lv_obj_align(RP.hint, LV_ALIGN_TOP_MID, 0, SERIN_PX(SERIN_SLOT_HINT_TOP));
    lv_obj_add_flag(RP.position, LV_OBJ_FLAG_HIDDEN);
    s_drawn = -1;
}

void room_source_destroy(void)
{
    memset(&RP, 0, sizeof RP);
    memset(&RR, 0, sizeof RR);
    s_drawn = -1;
}

void room_source_enter(void)
{
    s_pick = selected_index();
    s_source_removed = false;
    s_drawn = -1;
}

void refresh_room_source(void)
{
    if (!RP.panel) return;
    if (s_pick < 0 || s_pick > s_count) s_pick = selected_index();
    if (RP.first != ring_first()) {
        screen_rebuild(SCREEN_ROOM_SOURCE);
        return;
    }
    bool back = s_pick == s_count;
    if (s_drawn != s_pick) {
        uint32_t color = back ? COL_BACK : COL_ITEM_GREEN;
        rim_select(&RR, back ? RP.count : s_pick - RP.first, color);
        char name[SL2_ROOM_SOURCE_NAME_LEN + 1];
        if (!back) {
            memcpy(name, s_options[s_pick].name, SL2_ROOM_SOURCE_NAME_LEN);
            name[SL2_ROOM_SOURCE_NAME_LEN] = '\0';
        }
        lv_label_set_text(RP.name, back ? "Back" : name);
        lv_obj_set_style_text_color(RP.name, lv_color_hex(color), 0);
        if (!back && s_count > SOURCES_PER_RING) {
            lv_label_set_text_fmt(RP.position, "%d / %d", s_pick + 1, s_count);
            lv_obj_remove_flag(RP.position, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(RP.position, LV_OBJ_FLAG_HIDDEN);
        }
        s_drawn = s_pick;
    }
    bool selected = !back && s_options[s_pick].id == s_selected_id && s_status == SL2_ROOMST_OK;
    lv_label_set_text(RP.action, back ? "Return" : selected ? "Selected" : "Choose");
    lv_obj_set_style_text_color(RP.action, lv_color_hex(selected ? COL_ITEM_GREEN : COL_WHITE), 0);
    const char *hint = s_count == 0 ? "No sensors available"
                    : back && s_source_removed ? "Sensor no longer available" : "";
    lv_label_set_text(RP.hint, hint);
    if (hint[0]) lv_obj_remove_flag(RP.hint, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(RP.hint, LV_OBJ_FLAG_HIDDEN);
}

void room_source_turn(int delta)
{
    if (!delta) return;
    int pick = s_pick + (delta > 0 ? 1 : -1);
    s_pick = pick < 0 ? 0 : pick > s_count ? s_count : pick;
    s_source_removed = false;
    refresh_room_source();
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
    if (same) refresh_room_source();
    else      screen_rebuild(SCREEN_ROOM_SOURCE);
}

void ui_set_room_source_id(uint64_t selected_id, uint8_t status)
{
    if (selected_id == s_selected_id && status == s_status) return;
    s_selected_id = selected_id;
    s_status = status;
    if (s_cur == SCREEN_ROOM_SOURCE) refresh_room_source();
}
