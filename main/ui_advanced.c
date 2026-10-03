/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_widgets.h"
#include "ui_rim.h"
#include "icons.h"
#include "prefs.h"
#include "sl2_proto.h"
#include <stdio.h>
#include <string.h>

typedef enum { ADV_UNITS, ADV_LOCK, ADV_IDLE, ADV_NIGHT, ADV_TRANSITIONS,
               ADV_HEATPUMP, ADV_RESET, ADV_BACK, ADV_N } adv_row_t;
static const char *ADV_NAME[ADV_N] = { "Units", "Child Lock", "Idle screen", "Night", "Transitions",
                                      "Heat Pump", "Reset Link", "Back" };

static const uint32_t ADV_COL[ADV_N] = {
    COL_ADV_UNITS, COL_ADV_LOCK, COL_ADV_IDLE, COL_ADV_NIGHT, COL_ADV_TRANSITIONS,
    COL_ITEM_GREEN, COL_ALERT_NOLINK, COL_BACK
};
static const serin_icon_t *const ADV_ICON[ADV_N] = {
    &set_units, &set_lock, &set_idle, &set_night, &set_transitions,
    &set_system_chip, &set_reset, &nav_back
};

/* Rows whose name is too terse to explain itself get an info button and this
 * paragraph. A NULL entry is the whole scope test -- there is no second table
 * of "rows with an (i)" to drift out of step with the copy. */
static const char *const ADV_INFO[ADV_N] = {
    NULL,
    "Ignores turns and taps so settings can't change by accident. "
    "Hold the knob to unlock.",
    "What the panel shows when nobody is using Link. Glance keeps a dim "
    "temperature face; Off turns the screen fully dark and Link runs "
    "cooler, so room readings drift less.",
    "The controller flags night hours. While night is active Link caps "
    "its brightness near 20% so it doesn't glow in a dark room. Changing "
    "Brightness lifts the cap until Link goes idle.",
    "Fades between screens. Off changes screens immediately.",
    NULL, NULL, NULL
};

#define ADV_INFO_D        35   /* 34 px on the 466 panel */
#define ADV_INFO_RING      2
#define ADV_INFO_RING_OPA 41   /* white @ 16% */
#define ADV_INFO_GAP       4   /* keep the info hit floor clear of the options */
#define ADV_INFO_COL_PAD  27   /* >= the 26 px touch_floor grows the dot by */

_Static_assert(ADV_N <= RIM_MAX, "advanced rows exceed the rim widget");

static int s_adv_sel = ADV_UNITS;
static bool s_adv_editing = false;

static rim_t ADVR;
static struct { lv_obj_t *panel, *name, *val, *meta, *hint, *holdArc,
                         *uRow, *uF, *uC,
                         *uFHit, *uCHit,
                         *lRow, *lOff, *lOn,
                         *lOffHit, *lOnHit,
                         *iRow, *iG, *iO,
                         *iGHit, *iOHit,
                         *nRow, *nOff, *nOn,
                         *nOffHit, *nOnHit,
                         *tRow, *tOff, *tOn,
                         *tOffHit, *tOnHit,
                         *info; } AV;

static void adv_node_click_cb(lv_event_t *e);
static void adv_info_click_cb(lv_event_t *e);
static void adv_info_show(int row);

lv_obj_t *advanced_hold_arc(void) { return AV.holdArc; }

float advanced_row_angle(void) { return rim_stop_angle(ADV_RESET, ADV_N, ADV_BACK); }

bool advanced_on_reset_row(void) { return s_adv_sel == ADV_RESET; }

int  ui_adv_sel(void)            { return s_adv_sel; }
bool ui_adv_editing_units(void)  { return s_adv_editing && s_adv_sel == ADV_UNITS; }

/* The alert dialogs offer "Open Settings" to read the reported state, which
 * lives on this row -- land on it rather than wherever the rim was left. */
void advanced_land_heatpump(void)
{
    s_adv_sel = ADV_HEATPUMP;
    s_adv_editing = false;
    ui_show_screen(SCREEN_ADVANCED);
}

static void adv_toggle_pad(lv_obj_t *h)
{
    lv_obj_set_style_pad_left(h, SERIN_PX(14), 0);
    lv_obj_set_style_pad_right(h, SERIN_PX(14), 0);
    lv_obj_set_style_pad_top(h, SERIN_PX(6), 0);
    lv_obj_set_style_pad_bottom(h, SERIN_PX(6), 0);
    lv_obj_set_style_radius(h, SERIN_PX(20), 0);
}

static void adv_opt_objects(lv_obj_t *row, lv_obj_t **first, lv_obj_t **second,
                            lv_obj_t **first_hit, lv_obj_t **second_hit)
{
    if (row == AV.uRow) {
        *first = AV.uF; *second = AV.uC; *first_hit = AV.uFHit; *second_hit = AV.uCHit;
    } else if (row == AV.lRow) {
        *first = AV.lOff; *second = AV.lOn; *first_hit = AV.lOffHit; *second_hit = AV.lOnHit;
    } else if (row == AV.iRow) {
        *first = AV.iG; *second = AV.iO; *first_hit = AV.iGHit; *second_hit = AV.iOHit;
    } else if (row == AV.nRow) {
        *first = AV.nOff; *second = AV.nOn; *first_hit = AV.nOffHit; *second_hit = AV.nOnHit;
    } else {
        *first = AV.tOff; *second = AV.tOn; *first_hit = AV.tOffHit; *second_hit = AV.tOnHit;
    }
}

static void adv_opt_press_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_user_data(e), *a, *b, *a_hit, *b_hit;
    adv_opt_objects(row, &a, &b, &a_hit, &b_hit);
    lv_event_code_t c = lv_event_get_code(e);
    bool up = (c == LV_EVENT_RELEASED || c == LV_EVENT_PRESS_LOST);
    bool second = lv_event_get_target(e) == b_hit;
    lv_obj_set_state(a, LV_STATE_PRESSED, !up && !second);
    lv_obj_set_state(b, LV_STATE_PRESSED, !up &&  second);
}

static void lock_set(bool on)
{
    if (on == s_child_lock.enabled) return;
    if (on) child_lock_init(&s_child_lock, true);
    else    child_lock_disarm(&s_child_lock);
    prefs_set_u8("lk_en", on ? 1 : 0);
    prefs_erase("lk_min");
    prefs_erase("lk_max");
}

static void adv_opt_tap_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_user_data(e), *a, *b, *a_hit, *b_hit;
    adv_opt_objects(row, &a, &b, &a_hit, &b_hit);
    bool second = lv_event_get_target(e) == b_hit;
    s_adv_editing = true;
    if (row == AV.uRow) {
        bool nc = second;
        if (nc != s_units_c) {
            s_units_c = nc;
            notify_units(!s_units_c);
        }
    } else if (row == AV.lRow) {
        lock_set(second);
    } else if (row == AV.iRow) {
        ui_idle_off_set(second);
    } else if (row == AV.nRow) {
        ui_night_en_set(second);
    } else {
        ui_anim_en_set(second);
    }
    refresh_advanced();
}

static lv_obj_t *adv_opt_row(lv_obj_t *col, lv_obj_t **first, const char *ftxt,
                             lv_obj_t **first_hit, lv_obj_t **second, const char *stxt,
                             lv_obj_t **second_hit)
{
    static const touch_floor_arg_t k_advopt_tf = { 0, "AV.option" };
    lv_obj_t *row = mkrow(col, 0);
    *first_hit = lv_obj_create(row);
    *second_hit = lv_obj_create(row);
    for (lv_obj_t *hit = *first_hit; hit; hit = hit == *first_hit ? *second_hit : NULL) {
        lv_obj_remove_style_all(hit);
        lv_obj_set_size(hit, SERIN_TOUCH_MIN, SERIN_TOUCH_MIN);
        lv_obj_remove_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    }

    *first  = mklabel(*first_hit, FONT_UNITS, COL_WHITE, ftxt);
    *second = mklabel(*second_hit, FONT_UNITS, COL_GREY,  stxt);
    adv_toggle_pad(*first);
    adv_toggle_pad(*second);
    lv_obj_align(*first, LV_ALIGN_RIGHT_MID, -SERIN_PX(3), 0);
    lv_obj_align(*second, LV_ALIGN_LEFT_MID, SERIN_PX(3), 0);

    /* A minimum-size slot can be narrower than a padded word such as
     * "Glance". Lay out the compact visual pill first, then reserve enough
     * real parent width for it before the row is measured or made touchable. */
    lv_obj_update_layout(row);
    lv_coord_t fw = lv_obj_get_width(*first) + SERIN_PX(3);
    lv_coord_t sw = lv_obj_get_width(*second) + SERIN_PX(3);
    lv_obj_set_width(*first_hit, fw > SERIN_TOUCH_MIN ? fw : SERIN_TOUCH_MIN);
    lv_obj_set_width(*second_hit, sw > SERIN_TOUCH_MIN ? sw : SERIN_TOUCH_MIN);
    lv_obj_update_layout(row);

    press_ack(*first,  ACK_OPA_UNITS);
    press_ack(*second, ACK_OPA_UNITS);
    for (lv_obj_t *hit = *first_hit; hit; hit = hit == *first_hit ? *second_hit : NULL) {
        touch_floor(hit, &k_advopt_tf);
        lv_obj_add_event_cb(hit, adv_opt_tap_cb, LV_EVENT_CLICKED, row);
        lv_obj_add_event_cb(hit, adv_opt_press_cb, LV_EVENT_PRESSED,    row);
        lv_obj_add_event_cb(hit, adv_opt_press_cb, LV_EVENT_PRESSING,   row);
        lv_obj_add_event_cb(hit, adv_opt_press_cb, LV_EVENT_RELEASED,   row);
        lv_obj_add_event_cb(hit, adv_opt_press_cb, LV_EVENT_PRESS_LOST, row);
    }
    return row;
}

/* Neutral grey on purpose: it must not read as a third option next to the
 * row's own accent-coloured toggle. */
static lv_obj_t *adv_info_btn(lv_obj_t *col)
{
    static const touch_floor_arg_t k_advinfo_tf = { 0, "AV.info" };

    lv_obj_t *b = lv_obj_create(col);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, SERIN_PX(ADV_INFO_D), SERIN_PX(ADV_INFO_D));
    lv_obj_set_style_margin_top(b, SERIN_PX(ADV_INFO_GAP), 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b, SERIN_PX(ADV_INFO_RING), 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_border_opa(b, ADV_INFO_RING_OPA, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *l = mklabel(b, FONT_STATUS, COL_TEXT_SOFT, "i");
    lv_obj_set_style_text_opa(l, LV_OPA_50, 0);
    lv_obj_center(l);

    press_ack(b, ACK_OPA_UNITS);
    lv_obj_add_event_cb(b, adv_info_click_cb, LV_EVENT_CLICKED, NULL);
    touch_floor(b, &k_advinfo_tf);
    return b;
}

void build_advanced(void)
{
    lv_obj_t *p = AV.panel = s_panel[SCREEN_ADVANCED] = mkpanel(COL_FACE, false);

    float ang[ADV_N];
    for (int i = 0; i < ADV_N; i++)
        ang[i] = rim_stop_angle(i, ADV_N, ADV_BACK);

    rim_cfg_t cfg = {
        .n = ADV_N, .ang = ang, .col = ADV_COL, .glyph = ADV_ICON,
        .half_deg = 360.0f / ADV_N / 2.0f - 8.0f,
        .glyph_r = SERIN_GLYPH_R_SET,
        .disp_idle = 45, .disp_sel = 60, .node_px = 100, .on_tap = adv_node_click_cb,
        .no_ind = true,
    };
    rim_build(&ADVR, p, &cfg);

    /* The option slots already reserve the touch floor around each pill. */
    lv_obj_t *c = mkcol(p, SERIN_PX(300), 4, SERIN_PX(ADV_INFO_COL_PAD));
    lv_obj_align(c, LV_ALIGN_TOP_MID, 0, SERIN_PX(176));

    AV.name = mklabel(c, FONT_SET_NAME, COL_ADV_UNITS, "Units");
    AV.val  = mklabel(c, FONT_DIAL_PILL, COL_WHITE, "");
    AV.meta = mklabel(c, FONT_STATUS, COL_DIM, "");
    lv_obj_add_flag(AV.meta, LV_OBJ_FLAG_HIDDEN);

    AV.uRow = adv_opt_row(c, &AV.uF, "\xC2\xB0" "F", &AV.uFHit,
                          &AV.uC, "\xC2\xB0" "C", &AV.uCHit);
    lv_obj_add_flag(AV.uRow, LV_OBJ_FLAG_HIDDEN);
    AV.lRow = adv_opt_row(c, &AV.lOff, "Off", &AV.lOffHit,
                          &AV.lOn, "On", &AV.lOnHit);
    lv_obj_add_flag(AV.lRow, LV_OBJ_FLAG_HIDDEN);
    AV.iRow = adv_opt_row(c, &AV.iG, "Glance", &AV.iGHit,
                          &AV.iO, "Off", &AV.iOHit);
    lv_obj_add_flag(AV.iRow, LV_OBJ_FLAG_HIDDEN);
    AV.nRow = adv_opt_row(c, &AV.nOff, "Off", &AV.nOffHit,
                          &AV.nOn, "On", &AV.nOnHit);
    lv_obj_add_flag(AV.nRow, LV_OBJ_FLAG_HIDDEN);
    AV.tRow = adv_opt_row(c, &AV.tOff, "Off", &AV.tOffHit,
                          &AV.tOn, "On", &AV.tOnHit);
    lv_obj_add_flag(AV.tRow, LV_OBJ_FLAG_HIDDEN);

    AV.hint = mklabel(c, FONT_HINT, COL_GREY, "");
    lv_obj_set_style_text_align(AV.hint, LV_TEXT_ALIGN_CENTER, 0);

    AV.info = adv_info_btn(c);
    lv_obj_add_flag(AV.info, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *ha = AV.holdArc = rim_arc(p, ang[ADV_RESET], SERIN_IND_HALF_DEG,
                                       SERIN_PX(SERIN_RIM_W_SEL), COL_ALERT_NOLINK);
    lv_obj_add_flag(ha, LV_OBJ_FLAG_HIDDEN);

}

void advanced_destroy(void)
{
    memset(&AV, 0, sizeof AV);
    s_adv_editing = false;
}

void refresh_advanced(void)
{
    if (!AV.panel) return;

    rim_select(&ADVR, s_adv_sel, ADV_COL[s_adv_sel]);

    lv_label_set_text(AV.name, ADV_NAME[s_adv_sel]);
    lv_obj_set_style_text_color(AV.name, lv_color_hex(ADV_COL[s_adv_sel]), 0);

    lv_obj_add_flag(AV.uRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.lRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.iRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.nRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(AV.tRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_WHITE), 0);
    lv_obj_add_flag(AV.meta, LV_OBJ_FLAG_HIDDEN);

    if (ADV_INFO[s_adv_sel]) lv_obj_remove_flag(AV.info, LV_OBJ_FLAG_HIDDEN);
    else                     lv_obj_add_flag(AV.info, LV_OBJ_FLAG_HIDDEN);

    switch (s_adv_sel) {
    case ADV_UNITS: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.uRow, LV_OBJ_FLAG_HIDDEN);
        bool cc = s_units_c;
        lv_obj_set_style_bg_opa(AV.uF, cc ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.uF, lv_color_hex(COL_ADV_UNITS), 0);
        lv_obj_set_style_text_color(AV.uF, lv_color_hex(cc ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.uC, cc ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.uC, lv_color_hex(COL_ADV_UNITS), 0);
        lv_obj_set_style_text_color(AV.uC, lv_color_hex(cc ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_IDLE: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.iRow, LV_OBJ_FLAG_HIDDEN);
        bool off = ui_idle_off_get();
        lv_obj_set_style_bg_opa(AV.iG, off ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.iG, lv_color_hex(COL_ADV_IDLE), 0);
        lv_obj_set_style_text_color(AV.iG, lv_color_hex(off ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.iO, off ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.iO, lv_color_hex(COL_ADV_IDLE), 0);
        lv_obj_set_style_text_color(AV.iO, lv_color_hex(off ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_NIGHT: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.nRow, LV_OBJ_FLAG_HIDDEN);
        bool on = ui_night_en_get();
        lv_obj_set_style_bg_opa(AV.nOff, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.nOff, lv_color_hex(COL_ADV_NIGHT), 0);
        lv_obj_set_style_text_color(AV.nOff, lv_color_hex(on ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.nOn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.nOn, lv_color_hex(COL_ADV_NIGHT), 0);
        lv_obj_set_style_text_color(AV.nOn, lv_color_hex(on ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_TRANSITIONS: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.tRow, LV_OBJ_FLAG_HIDDEN);
        bool on = ui_anim_en_get();
        lv_obj_set_style_bg_opa(AV.tOff, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.tOff, lv_color_hex(COL_ADV_TRANSITIONS), 0);
        lv_obj_set_style_text_color(AV.tOff, lv_color_hex(on ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.tOn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.tOn, lv_color_hex(COL_ADV_TRANSITIONS), 0);
        lv_obj_set_style_text_color(AV.tOn, lv_color_hex(on ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_LOCK: {
        lv_obj_add_flag(AV.val, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(AV.lRow, LV_OBJ_FLAG_HIDDEN);
        bool on = s_child_lock.enabled;
        lv_obj_set_style_bg_opa(AV.lOff, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(AV.lOff, lv_color_hex(COL_ADV_LOCK), 0);
        lv_obj_set_style_text_color(AV.lOff, lv_color_hex(on ? COL_GREY : COL_ON_LIGHT), 0);
        lv_obj_set_style_bg_opa(AV.lOn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(AV.lOn, lv_color_hex(COL_ADV_LOCK), 0);
        lv_obj_set_style_text_color(AV.lOn, lv_color_hex(on ? COL_ON_LIGHT : COL_GREY), 0);
        break;
    }
    case ADV_HEATPUMP: {
        const hvac_state_t *s = &s_state;
        if (!s->cn105_connected) {
            lv_label_set_text(AV.val, "Not connected");
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_ALERT_NOLINK), 0);
        } else if (s->error_code) {
            char code[8], text[24];
            fault_code_str(code, sizeof code, s->error_code);
            snprintf(text, sizeof text, "Fault %s", code);
            lv_label_set_text(AV.val, text);
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_ALERT_WARN), 0);
        } else if (s->err_count) {
            lv_label_set_text_fmt(AV.val, "%u error%s", (unsigned)s->err_count,
                                  s->err_count == 1 ? "" : "s");
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_ALERT_NOLINK), 0);
        } else {
            lv_label_set_text(AV.val, "Connected");
            lv_obj_set_style_text_color(AV.val, lv_color_hex(COL_GREEN), 0);
        }
        if (s->cn105_connected) {
            const char *sm = (s->sub_mode && s->sub_mode[0]) ? s->sub_mode : NULL;
            char meta[48]; size_t off = 0;
            meta[0] = '\0';
            if (sm) off += (size_t)snprintf(meta + off, sizeof meta - off, "%s", sm);
            if ((s_caps.features & SL2_FEAT_COMPRESSOR) && s->diag_seen)
                off += (size_t)snprintf(meta + off, sizeof meta - off,
                                        "%s%d Hz", off ? " \xC2\xB7 " : "", s->comp_freq_hz);
            if (s->outside_valid && off < sizeof meta)
                off += (size_t)snprintf(meta + off, sizeof meta - off,
                                        "%s%d\xC2\xB0 out", off ? " \xC2\xB7 " : "", s->outside_temp);
            if (off) {
                lv_label_set_text(AV.meta, meta);
                lv_obj_remove_flag(AV.meta, LV_OBJ_FLAG_HIDDEN);
            }
        }
        break;
    }
    default:        lv_label_set_text(AV.val, ""); break;
    }

    const char *hint = "";
    if (s_adv_sel == ADV_RESET)
        hint = H_HOLD("reset");
    else if (s_adv_sel == ADV_LOCK && s_child_lock.enabled && !s_adv_editing)
        hint = H_HOLD("unlock") " on the home screen";
    else if (s_adv_sel == ADV_HEATPUMP)
        hint = zones_can_enter() ? H_PRESS("choose") : "";
    else if (s_adv_editing)
        hint = H_PRESS("return");
    lv_label_set_text(AV.hint, hint);
    if (hint[0]) lv_obj_remove_flag(AV.hint, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(AV.hint, LV_OBJ_FLAG_HIDDEN);
}

void advanced_turn(int delta)
{
    if (s_adv_editing && s_adv_sel == ADV_UNITS) {
        if (delta) {
            bool nc = (delta > 0);
            if (nc != s_units_c) {
                s_units_c = nc;
                notify_units(!s_units_c);
            }
        }
    } else if (s_adv_editing && s_adv_sel == ADV_LOCK) {
        if (delta) lock_set(delta > 0);
    } else if (s_adv_editing && s_adv_sel == ADV_IDLE) {
        if (delta) ui_idle_off_set(delta > 0);
    } else if (s_adv_editing && s_adv_sel == ADV_NIGHT) {
        if (delta) ui_night_en_set(delta > 0);
    } else if (s_adv_editing && s_adv_sel == ADV_TRANSITIONS) {
        if (delta) ui_anim_en_set(delta > 0);
    } else if (!s_adv_editing) {
        s_adv_sel = ((s_adv_sel + delta) % ADV_N + ADV_N) % ADV_N;
    }
    refresh_advanced();
}

void advanced_press(void)
{
    switch (s_adv_sel) {
    case ADV_UNITS: s_adv_editing = !s_adv_editing; break;
    case ADV_LOCK:  s_adv_editing = !s_adv_editing; break;
    case ADV_IDLE:  s_adv_editing = !s_adv_editing; break;
    case ADV_NIGHT: s_adv_editing = !s_adv_editing; break;
    case ADV_TRANSITIONS: s_adv_editing = !s_adv_editing; break;
    case ADV_HEATPUMP:
        if (zones_can_enter()) ui_show_screen(SCREEN_ZONES);
        return;
    case ADV_RESET: break;
    case ADV_BACK:  settings_land(SET_ADVANCED); return;
    default: break;
    }
    refresh_advanced();
}

/* Explains the row; deliberately touches neither the selection nor the toggle. */
static void adv_info_show(int row)
{
    if (row < 0 || row >= ADV_N || !ADV_INFO[row]) return;
    info_dialog_show(ADV_COL[row], ADV_ICON[row], ADV_NAME[row], ADV_INFO[row]);
}

static void adv_info_click_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    adv_info_show(s_adv_sel);
}

static void adv_node_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    s_adv_sel = idx;
    s_adv_editing = false;
    if (idx == ADV_BACK) { settings_land(SET_ADVANCED); return; }
    refresh_advanced();
}
