/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
#include "icons.h"
#include "espnow_client.h"
#include "sl2_proto.h"
#include "link_ota.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"

#if CONFIG_SERIN_BOARD_VIEWE_15
#define SERIN_BOARD_NAME "Serin Link 1.5\""
#else
#define SERIN_BOARD_NAME "Serin Link 2.1\""
#endif

static void ota_btn_click_cb(lv_event_t *e);

static touch_floor_arg_t s_up_btn_tf  = { TF_CAP_PILL_ROW, "UP.btn"  };
static touch_floor_arg_t s_up_btn2_tf = { TF_CAP_PILL_ROW, "UP.btn2" };
static touch_floor_arg_t s_ab_upd_tf  = { TF_CAP_PILL_ROW, "AB.upd"  };
static touch_floor_arg_t s_ab_back_tf = { TF_CAP_PILL_ROW, "AB.back" };

static struct { lv_obj_t *icon, *ver, *big, *cap, *btn, *btnlbl,
                         *btn2, *btn2lbl, *hint, *arc; } UP;

/* Knob focus on the Update screen: 0 = primary pill, 1 = Cancel. Only the
 * confirm state shows both, so focus only moves there; it resets to the
 * primary pill whenever the OTA state changes. */
static int        s_up_sel;
static ota_ui_t   s_up_sel_st = OTA_UI_HIDDEN;

static bool update_two_pills(void) { return s_ota_st == OTA_UI_CONFIRM; }

static void ota_btn2_click_cb(lv_event_t *e)
{
    (void)e;
    notify_ota_cancel();
}

void build_update(void)
{
    lv_obj_t *p = s_panel[SCREEN_UPDATE] = mkpanel(COL_FACE, false);

    UP.arc = lv_arc_create(p);
    lv_obj_remove_style(UP.arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(UP.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(UP.arc, SERIN_PX(440), SERIN_PX(440));
    lv_obj_center(UP.arc);
    lv_arc_set_rotation(UP.arc, 270);
    lv_arc_set_bg_angles(UP.arc, 0, 360);
    lv_arc_set_range(UP.arc, 0, 100);
    lv_arc_set_value(UP.arc, 0);
    lv_obj_set_style_arc_width(UP.arc, SERIN_PX(8), LV_PART_MAIN);
    lv_obj_set_style_arc_width(UP.arc, SERIN_PX(8), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(UP.arc, OPA_FILL_NEUTRAL, LV_PART_MAIN);
    lv_obj_set_style_arc_color(UP.arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(UP.arc, lv_color_hex(COL_GREEN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(UP.arc, true, LV_PART_INDICATOR);

    lv_obj_t *c = mkcol(p, SERIN_PX(340), 14, 0);
    lv_obj_center(c);

    UP.icon = mkicon(c, &serin_logo, SERIN_PX(60), COL_HEAT_GLOW);
    lv_obj_add_flag(UP.icon, LV_OBJ_FLAG_HIDDEN);

    mklabel(c, FONT_SET_NAME, SET_COL[SET_ABOUT], "Update");
    UP.ver = mklabel(c, FONT_STATUS, COL_DIM, "");
    UP.big = mklabel(c, FONT_MODE_NAME, COL_WHITE, "");
    UP.cap = mkbody(c, FONT_STATUS_SM, COL_DIM, "", SERIN_PX(300));

    lv_obj_t *row = mkpillrow(c, TF_GAP_PILL_ROW, 0);

    UP.btn = mkchip_btn(row, "Install", COL_ON_LIGHT, SET_COL[SET_ABOUT],
                        ota_btn_click_cb, &s_up_btn_tf);
    lv_obj_set_style_outline_color(UP.btn, lv_color_white(), 0);
    lv_obj_set_style_outline_pad(UP.btn, SERIN_PX(3), 0);
    UP.btnlbl = lv_obj_get_child(UP.btn, 0);

    UP.btn2 = mkchip_btn(row, "Cancel", COL_WHITE, 0,
                         ota_btn2_click_cb, &s_up_btn2_tf);
    lv_obj_set_style_outline_color(UP.btn2, lv_color_white(), 0);
    lv_obj_set_style_outline_pad(UP.btn2, SERIN_PX(3), 0);
    UP.btn2lbl = lv_obj_get_child(UP.btn2, 0);

    UP.hint = mklabel(c, FONT_HINT, COL_GREY, "");
}

void refresh_update(void)
{
    const char *cur = s_fw_ver[0] ? s_fw_ver : "?";
    char ver[sizeof(s_fw_ver) + sizeof(s_ota_txt) + 8];
    char big[sizeof(s_ota_txt) + 2];
    const char *cap = "";
    const char *b1 = NULL, *b2 = NULL, *h = NULL;
    uint32_t bigcol = COL_WHITE;
    bool arc = false;
    const serin_icon_t *icn = NULL;
    uint32_t icol = 0;
    int ipx = 0;

    if (s_ota_st != s_up_sel_st) { s_up_sel = 0; s_up_sel_st = s_ota_st; }

    switch (s_ota_st) {
        case OTA_UI_CHECKING:
            snprintf(ver, sizeof ver, "v%s", cur);
            snprintf(big, sizeof big, "Checking");
            cap = "Contacting the update server";
            b2 = "Cancel"; h = H_PRESS("cancel");
            break;
        case OTA_UI_CONFIRM:
            /* Big line already states the new version; small line stays on the current one. */
            snprintf(ver, sizeof ver, "v%s", cur);
            snprintf(big, sizeof big, "v%s", s_ota_txt);
            bigcol = COL_GREEN;
            cap = "Update available";
            b1 = "Install"; b2 = "Cancel";
            h = s_up_sel ? H_PRESS("cancel") : H_PRESS("install");
            break;
        case OTA_UI_DOWNLOAD:
            snprintf(ver, sizeof ver, "v%s \xE2\x86\x92 v%s", cur, s_ota_txt);
            snprintf(big, sizeof big, "%d%%", s_ota_pct);
            cap = "Downloading";
            b2 = "Cancel"; h = H_PRESS("cancel");
            arc = true;
            break;
        case OTA_UI_VERIFY:
            snprintf(ver, sizeof ver, "v%s \xE2\x86\x92 v%s", cur, s_ota_txt);
            snprintf(big, sizeof big, "Verifying");
            cap = "Checking the downloaded image";
            arc = true;
            break;
        case OTA_UI_OK:
            snprintf(ver, sizeof ver, "v%s \xE2\x86\x92 v%s", cur, s_ota_txt);
            snprintf(big, sizeof big, "Installed");
            bigcol = COL_GREEN;
            cap = "Restarting. Keep the dial plugged in for 30 seconds";
            icn = &serin_logo; icol = COL_HEAT_GLOW; ipx = 60;
            break;
        case OTA_UI_UPTODATE:
            snprintf(ver, sizeof ver, "v%s", cur);
            snprintf(big, sizeof big, "Up to date");
            bigcol = COL_GREEN;
            b1 = "Done"; h = H_PRESS("dismiss");
            icn = &serin_logo; icol = COL_HEAT_GLOW; ipx = 60;
            break;
        default:
            snprintf(ver, sizeof ver, "v%s", cur);
            snprintf(big, sizeof big, "Failed");
            bigcol = COL_ALERT_NOLINK;
            cap = s_ota_txt[0] ? s_ota_txt : "Update failed";
            b1 = "Done"; h = H_PRESS("dismiss");
            icn = &hero_alert; icol = COL_ALERT_NOLINK; ipx = 36;
            break;
    }

    if (icn) {
        icon_fit(UP.icon, icn, SERIN_PX(ipx));
        lv_obj_set_style_image_recolor(UP.icon, lv_color_hex(icol), 0);
        lv_obj_remove_flag(UP.icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(UP.icon, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(UP.ver, ver);
    lv_label_set_text(UP.big, big);
    lv_obj_set_style_text_color(UP.big, lv_color_hex(bigcol), 0);
    lv_label_set_text(UP.cap, cap);
    if (cap[0]) lv_obj_remove_flag(UP.cap, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(UP.cap, LV_OBJ_FLAG_HIDDEN);
    if (b1) { lv_obj_remove_flag(UP.btn, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(UP.btnlbl, b1); }
    else    lv_obj_add_flag(UP.btn, LV_OBJ_FLAG_HIDDEN);
    if (b2) { lv_obj_remove_flag(UP.btn2, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(UP.btn2lbl, b2); }
    else    lv_obj_add_flag(UP.btn2, LV_OBJ_FLAG_HIDDEN);
    bool two = update_two_pills();
    lv_obj_set_style_outline_width(UP.btn,  (two && s_up_sel == 0) ? SERIN_PX(3) : 0, 0);
    lv_obj_set_style_outline_width(UP.btn2, (two && s_up_sel == 1) ? SERIN_PX(3) : 0, 0);
    if (h) { lv_obj_remove_flag(UP.hint, LV_OBJ_FLAG_HIDDEN); lv_label_set_text(UP.hint, h); }
    else   lv_obj_add_flag(UP.hint, LV_OBJ_FLAG_HIDDEN);
    if (arc) { lv_obj_remove_flag(UP.arc, LV_OBJ_FLAG_HIDDEN); lv_arc_set_value(UP.arc, s_ota_pct); }
    else     lv_obj_add_flag(UP.arc, LV_OBJ_FLAG_HIDDEN);
    touch_floor_pair(UP.btn, &s_up_btn_tf, UP.btn2, &s_up_btn2_tf, TF_CAP_PILL_ROW);
}

enum {
    AB_D_FW, AB_D_BUILD, AB_D_BOARD, AB_D_MAC, AB_D_UP, AB_D_RESET, AB_D_PROTO,
    AB_U_FW, AB_U_BUILD, AB_U_MAC, AB_U_UP, AB_U_RESET, AB_U_IP, AB_U_SSID, AB_U_RSSI, AB_U_CH,
    AB_L_STATE, AB_L_RSSI, AB_L_SEEN,
    AB_H_CN, AB_H_MODE, AB_H_LEAN, AB_H_HZ, AB_H_OUT, AB_H_RUN, AB_H_ERR,
    AB_S_TEMP, AB_S_HUM, AB_S_BATT, AB_S_RSSI,
    AB_LS_TEMP, AB_LS_HUM,
    AB_N
};
#define AB_DASH "\xE2\x80\x94"

static struct { lv_obj_t *cont, *sensHdr, *lsensHdr, *upd, *updlbl, *back, *backlbl,
                         *scrollArc, *val[AB_N], *row[AB_N]; } AB;
static int s_ab_sel;
static lv_timer_t *s_ab_tick;

static lv_obj_t *about_row(lv_obj_t *parent, const char *name, int idx)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    mklabel(r, FONT_STATUS, COL_DIM, name);
    AB.val[idx] = mklabel(r, FONT_STATUS, COL_WHITE, AB_DASH);
    lv_obj_set_style_max_width(AB.val[idx], SERIN_PX(176), 0);
    lv_label_set_long_mode(AB.val[idx], LV_LABEL_LONG_DOT);
    AB.row[idx] = r;
    return r;
}

static lv_obj_t *about_hdr(lv_obj_t *parent, const char *t)
{
    lv_obj_t *h = mklabel(parent, FONT_DIAL_CHIP, COL_DIM, t);
    lv_obj_set_style_pad_top(h, SERIN_PX(16), 0);
    lv_obj_set_style_text_letter_space(h, SERIN_PX(2), 0);
    return h;
}

static const char *about_reset_str(uint8_t r)
{
    switch (r) {
        case 1: return "Power-on";
        case 3: return "Software";
        case 4: return "Crash";
        case 5: case 6: case 7: return "Watchdog";
        case 9: return "Brownout";
        default: return "Other";
    }
}

static bool about_reset_abnormal(uint8_t r) { return r == 4 || r == 5 || r == 6 || r == 7 || r == 9; }

static void about_fmt_uptime(char *o, size_t n, uint32_t s)
{
    if (s >= 86400)    snprintf(o, n, "%ud %uh", (unsigned)(s / 86400), (unsigned)(s % 86400 / 3600));
    else if (s >= 3600) snprintf(o, n, "%uh %um", (unsigned)(s / 3600), (unsigned)(s % 3600 / 60));
    else                snprintf(o, n, "%um %us", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void about_focus_apply(void)
{
    lv_obj_set_style_outline_width(AB.upd,  s_ab_sel == 1 ? SERIN_PX(3) : 0, 0);
    lv_obj_set_style_outline_width(AB.back, s_ab_sel == 2 ? SERIN_PX(3) : 0, 0);
}

static void about_start_update_cb(void *unused)
{
    (void)unused;
    if (s_cur != SCREEN_ABOUT) return;
    if (!s_link.online || !ota_check_available()) return;
    s_ota_st = OTA_UI_CHECKING;
    ui_show_screen(SCREEN_UPDATE);
    notify_ota_check();
}
static void about_start_update(void) { lv_async_call(about_start_update_cb, NULL); }

void about_press(void)
{
    if (s_ab_sel == 1) { about_start_update(); return; }
    settings_land(SET_ABOUT);
}
static void about_back_cb(lv_event_t *e) { (void)e; s_ab_sel = 2; about_press(); }
static void about_upd_cb(lv_event_t *e)  { (void)e; about_start_update(); }

static void about_scroll_cb(lv_event_t *e)
{
    (void)e;
    if (!AB.scrollArc) return;
    int32_t up = lv_obj_get_scroll_top(AB.cont);
    int32_t dn = lv_obj_get_scroll_bottom(AB.cont);
    int32_t span = up + dn;
    lv_arc_set_value(AB.scrollArc, span > 0 ? (int32_t)(1000 * up / span) : 0);
}

static void about_bg_cb(lv_event_t *e) { (void)e; s_ab_sel = 2; about_press(); }

void refresh_about(void);

static void about_refresh_time(void)
{
    if (!AB.cont) return;
    char b[48];
    about_fmt_uptime(b, sizeof b, (uint32_t)(esp_timer_get_time() / 1000000));
    lv_label_set_text(AB.val[AB_D_UP], b);
    int age = espnow_client_last_rx_age_s();
    if (age < 0)       lv_label_set_text(AB.val[AB_L_SEEN], AB_DASH);
    else if (age <= 1) lv_label_set_text(AB.val[AB_L_SEEN], "Now");
    else               lv_label_set_text_fmt(AB.val[AB_L_SEEN], "%ds ago", age);
}
static void about_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_cur != SCREEN_ABOUT) return;
    about_refresh_time();
}

void build_about(void)
{
    lv_obj_t *p = s_panel[SCREEN_ABOUT] = mkpanel(COL_FACE, false);
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p, about_bg_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = mklabel(p, FONT_SET_NAME, SET_COL[SET_ABOUT], "About");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SERIN_PX(46));

    AB.scrollArc = lv_arc_create(p);
    int asz = 2 * SERIN_PX(SERIN_RIM_R) + SERIN_PX(SERIN_RIM_W_SEL);
    lv_obj_set_size(AB.scrollArc, asz, asz);
    lv_obj_center(AB.scrollArc);
    lv_obj_remove_style(AB.scrollArc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(AB.scrollArc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_rotation(AB.scrollArc, 270);
    lv_arc_set_bg_angles(AB.scrollArc, 0, 360);
    lv_arc_set_range(AB.scrollArc, 0, 1000);
    lv_arc_set_value(AB.scrollArc, 0);
    lv_obj_set_style_arc_width(AB.scrollArc, SERIN_PX(SERIN_RIM_W_IDLE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(AB.scrollArc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(AB.scrollArc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(AB.scrollArc, SERIN_PX(SERIN_RIM_W_SEL), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(AB.scrollArc, lv_color_hex(SET_COL[SET_ABOUT]), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(AB.scrollArc, true, LV_PART_INDICATOR);

    AB.cont = lv_obj_create(p);
    lv_obj_remove_style_all(AB.cont);
    lv_obj_set_size(AB.cont, SERIN_PX(280), SERIN_PX(262));
    lv_obj_align(AB.cont, LV_ALIGN_TOP_MID, 0, SERIN_PX(94));
    lv_obj_set_flex_flow(AB.cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(AB.cont, SERIN_PX(8), 0);
    lv_obj_set_scroll_dir(AB.cont, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(AB.cont, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(AB.cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(AB.cont, about_scroll_cb, LV_EVENT_SCROLL, NULL);

    about_hdr(AB.cont, "LINK");
    about_row(AB.cont, "Firmware",  AB_D_FW);
    about_row(AB.cont, "Build",     AB_D_BUILD);
    about_row(AB.cont, "Board",     AB_D_BOARD);
    about_row(AB.cont, "MAC",       AB_D_MAC);
    about_row(AB.cont, "Uptime",    AB_D_UP);
    about_row(AB.cont, "Last boot", AB_D_RESET);
    about_row(AB.cont, "Protocol",  AB_D_PROTO);

    uint16_t feat = s_caps.features;
    about_hdr(AB.cont, "CONTROLLER");
    if (feat & SL2_FEAT_FW_INFO) {
        about_row(AB.cont, "Firmware",  AB_U_FW);
        about_row(AB.cont, "Build",     AB_U_BUILD);
    }
    about_row(AB.cont, "MAC",       AB_U_MAC);
    if (feat & SL2_FEAT_FW_INFO) {
        about_row(AB.cont, "Uptime",    AB_U_UP);
        about_row(AB.cont, "Last boot", AB_U_RESET);
    }
    if (feat & SL2_FEAT_WIFI_INFO) {
        about_row(AB.cont, "IP",        AB_U_IP);
        about_row(AB.cont, "Wi-Fi",     AB_U_SSID);
        about_row(AB.cont, "Signal",    AB_U_RSSI);
        about_row(AB.cont, "Channel",   AB_U_CH);
    }

    about_hdr(AB.cont, "CONNECTION");
    about_row(AB.cont, "Status",    AB_L_STATE);
    about_row(AB.cont, "Signal",    AB_L_RSSI);
    about_row(AB.cont, "Last seen", AB_L_SEEN);

    about_hdr(AB.cont, "HEAT PUMP");
    about_row(AB.cont, "CN105",      AB_H_CN);
    if (feat & SL2_FEAT_COMPRESSOR) {
        about_row(AB.cont, "Stage",      AB_H_MODE);
        about_row(AB.cont, "Auto lean",  AB_H_LEAN);
        about_row(AB.cont, "Compressor", AB_H_HZ);
    }
    if (feat & SL2_FEAT_OUTSIDE_T)
        about_row(AB.cont, "Outdoor",    AB_H_OUT);
    if (feat & SL2_FEAT_RUNTIME)
        about_row(AB.cont, "Runtime",    AB_H_RUN);
    about_row(AB.cont, "Errors",     AB_H_ERR);

    if (feat & SL2_FEAT_SENSOR_BATT) {
        AB.sensHdr = about_hdr(AB.cont, "SENSOR");
        about_row(AB.cont, "Temperature", AB_S_TEMP);
        about_row(AB.cont, "Humidity",    AB_S_HUM);
        about_row(AB.cont, "Battery",     AB_S_BATT);
        about_row(AB.cont, "Signal",      AB_S_RSSI);
    }

    if (s_state.link_sens_present) {
        AB.lsensHdr = about_hdr(AB.cont, "LINK SENSOR");
        about_row(AB.cont, "Temperature", AB_LS_TEMP);
        about_row(AB.cont, "Humidity",    AB_LS_HUM);
    }

    static const int wide[] = { AB_D_MAC, AB_U_MAC, AB_U_IP, AB_U_SSID,
                                AB_D_BUILD, AB_U_BUILD };
    for (unsigned i = 0; i < sizeof wide / sizeof wide[0]; i++)
        if (AB.val[wide[i]])
            lv_obj_set_style_max_width(AB.val[wide[i]], SERIN_PX(212), 0);

    /* Fade the last visible rows out instead of clipping them mid-glyph behind
       the floating Update/Back buttons. Created after AB.cont and before the
       button row so it paints above the list and below the buttons. */
    lv_obj_t *fade = lv_obj_create(p);
    lv_obj_remove_style_all(fade);
    lv_obj_set_size(fade, SERIN_PX(280), SERIN_PX(40));
    lv_obj_align(fade, LV_ALIGN_TOP_MID, 0, SERIN_PX(94 + 262 - 40));
    lv_obj_set_style_bg_color(fade, lv_color_hex(COL_FACE), 0);
    lv_obj_set_style_bg_grad_color(fade, lv_color_hex(COL_FACE), 0);
    lv_obj_set_style_bg_grad_dir(fade, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(fade, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(fade, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(fade, LV_OPA_COVER, 0);
    lv_obj_remove_flag(fade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(fade, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *row = lv_obj_create(p);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, TF_GAP_PILL_ROW, 0);
    lv_obj_set_style_pad_all(row, TF_ROW_PAD, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(58) + TF_ROW_PAD);

    AB.upd = mkchip_btn(row, "Update", COL_ON_LIGHT, SET_COL[SET_ABOUT],
                        about_upd_cb, &s_ab_upd_tf);
    lv_obj_set_style_outline_color(AB.upd, lv_color_white(), 0);
    lv_obj_set_style_outline_pad(AB.upd, SERIN_PX(3), 0);
    AB.updlbl = lv_obj_get_child(AB.upd, 0);

    AB.back = mkchip_btn(row, "Back", COL_WHITE, 0, about_back_cb, &s_ab_back_tf);
    lv_obj_set_style_outline_color(AB.back, lv_color_white(), 0);
    lv_obj_set_style_outline_pad(AB.back, SERIN_PX(3), 0);
    AB.backlbl = lv_obj_get_child(AB.back, 0);

    s_ab_tick = lv_timer_create(about_tick_cb, 1000, NULL);
}

void about_destroy(void)
{
    if (s_ab_tick) { lv_timer_delete(s_ab_tick); s_ab_tick = NULL; }
    memset(&AB, 0, sizeof(AB));
}

void refresh_about(void)
{
    if (!AB.cont) return;
    const hvac_state_t *s = &s_state;
    char b[48];
    uint8_t m[6];

    lv_label_set_text_fmt(AB.val[AB_D_FW], "v%s", s_fw_ver[0] ? s_fw_ver : "?");
    lv_label_set_text(AB.val[AB_D_BUILD], s_fw_build[0] ? s_fw_build : AB_DASH);
    lv_label_set_text(AB.val[AB_D_BOARD], SERIN_BOARD_NAME);
    espnow_client_self_mac(m);
    lv_label_set_text_fmt(AB.val[AB_D_MAC], "%02X:%02X:%02X:%02X:%02X:%02X",
                          m[0], m[1], m[2], m[3], m[4], m[5]);
    uint8_t dr = (uint8_t)esp_reset_reason();
    lv_label_set_text(AB.val[AB_D_RESET], about_reset_str(dr));
    lv_obj_set_style_text_color(AB.val[AB_D_RESET],
        lv_color_hex(about_reset_abnormal(dr) ? COL_ALERT_WARN : COL_WHITE), 0);
    uint8_t pv = espnow_client_peer_version();
    if (pv) lv_label_set_text_fmt(AB.val[AB_D_PROTO], "v%u \xE2\x86\x94 v%u",
                                  SL2_PROTO_VERSION, pv);
    else    lv_label_set_text_fmt(AB.val[AB_D_PROTO], "v%u \xE2\x86\x94 " AB_DASH,
                                  SL2_PROTO_VERSION);
    lv_obj_set_style_text_color(AB.val[AB_D_PROTO],
        lv_color_hex((pv && pv != SL2_PROTO_VERSION) ? COL_ALERT_WARN : COL_WHITE), 0);

    if (AB.row[AB_U_FW]) {
        if (s->diag_seen) {
            const char *ufw = s->unit_fw;
            if (ufw[0] == 'v' || ufw[0] == 'V') ufw++;
            lv_label_set_text_fmt(AB.val[AB_U_FW], "v%s", ufw);
        } else lv_label_set_text(AB.val[AB_U_FW], AB_DASH);
        lv_label_set_text(AB.val[AB_U_BUILD], s->diag_seen ? s->unit_build : AB_DASH);
    }
    if (s_link.bonded)
        lv_label_set_text_fmt(AB.val[AB_U_MAC], "%02X:%02X:%02X:%02X:%02X:%02X",
                              s_link.mac[0], s_link.mac[1], s_link.mac[2],
                              s_link.mac[3], s_link.mac[4], s_link.mac[5]);
    else lv_label_set_text(AB.val[AB_U_MAC], AB_DASH);
    if (AB.row[AB_U_UP]) {
        if (s->diag_seen) { about_fmt_uptime(b, sizeof b, s->unit_uptime_s);
                            lv_label_set_text(AB.val[AB_U_UP], b); }
        else lv_label_set_text(AB.val[AB_U_UP], AB_DASH);
        lv_label_set_text(AB.val[AB_U_RESET],
                          s->diag_seen ? about_reset_str(s->unit_reset_reason) : AB_DASH);
        lv_obj_set_style_text_color(AB.val[AB_U_RESET],
            lv_color_hex((s->diag_seen && about_reset_abnormal(s->unit_reset_reason))
                         ? COL_ALERT_WARN : COL_WHITE), 0);
    }
    if (AB.row[AB_U_IP]) {
        lv_label_set_text(AB.val[AB_U_IP],   s->wifi_ip[0]   ? s->wifi_ip   : AB_DASH);
        lv_label_set_text(AB.val[AB_U_SSID], s->wifi_ssid[0] ? s->wifi_ssid : AB_DASH);
        if (s->wifi_connected) lv_label_set_text_fmt(AB.val[AB_U_RSSI], "%d dBm", s->wifi_rssi);
        else                   lv_label_set_text(AB.val[AB_U_RSSI], AB_DASH);
        if (s->diag_seen && s->unit_wifi_ch)
            lv_label_set_text_fmt(AB.val[AB_U_CH], "%u", (unsigned)s->unit_wifi_ch);
        else lv_label_set_text(AB.val[AB_U_CH], AB_DASH);
    }

    lv_label_set_text(AB.val[AB_L_STATE], s_link.online ? "Online" : "Offline");
    lv_obj_set_style_text_color(AB.val[AB_L_STATE],
        lv_color_hex(s_link.online ? COL_GREEN : COL_ALERT_NOLINK), 0);
    if (s_link.online) lv_label_set_text_fmt(AB.val[AB_L_RSSI], "%d dBm", s_link.rssi);
    else                 lv_label_set_text(AB.val[AB_L_RSSI], AB_DASH);
    about_refresh_time();

    lv_label_set_text(AB.val[AB_H_CN], !s_link.online ? AB_DASH
                      : (s->cn105_connected ? "Connected" : "Not connected"));
    if (AB.row[AB_H_MODE]) {
        if (!s_link.online) {
            lv_label_set_text(AB.val[AB_H_MODE], AB_DASH);
            lv_label_set_text(AB.val[AB_H_HZ],   AB_DASH);
        } else {
            const char *sm = (s->sub_mode && s->sub_mode[0]) ? s->sub_mode : NULL;
            const char *st = (s->diag_seen && s->comp_stage && s->comp_stage[0])
                             ? s->comp_stage : AB_DASH;
            if (sm) lv_label_set_text_fmt(AB.val[AB_H_MODE], "%s \xC2\xB7 %s", sm, st);
            else    lv_label_set_text(AB.val[AB_H_MODE], st);
            if (s->diag_seen)
                lv_label_set_text_fmt(AB.val[AB_H_HZ], "%d Hz", s->comp_freq_hz);
            else
                lv_label_set_text(AB.val[AB_H_HZ], AB_DASH);
        }
        static const char *const LEAN[4] = { AB_DASH, "Cooling", "Heating", "Leader" };
        lv_label_set_text(AB.val[AB_H_LEAN],
            (s->diag_seen && s->auto_sub_mode < 4) ? LEAN[s->auto_sub_mode] : AB_DASH);
    }
    if (AB.row[AB_H_OUT]) {
        if (s_link.online && s->outside_valid)
            lv_label_set_text_fmt(AB.val[AB_H_OUT], "%d\xC2\xB0", s->outside_temp);
        else lv_label_set_text(AB.val[AB_H_OUT], AB_DASH);
    }
    if (AB.row[AB_H_RUN]) {
        if (s->diag_seen && s->runtime_valid)
            lv_label_set_text_fmt(AB.val[AB_H_RUN], "%u h", (unsigned)s->runtime_h);
        else lv_label_set_text(AB.val[AB_H_RUN], AB_DASH);
    }
    bool fault = (s->error_code != 0);
    if (fault) {
        char code[8];
        fault_code_str(code, sizeof code, s->error_code);
        lv_label_set_text(AB.val[AB_H_ERR], code);
    } else {
        lv_label_set_text(AB.val[AB_H_ERR], "None");
    }
    lv_obj_set_style_text_color(AB.val[AB_H_ERR],
        lv_color_hex(fault ? COL_ALERT_NOLINK : COL_WHITE), 0);

    bool sens = AB.sensHdr && (s->ble_valid || s->sensor_batt_pct >= 0);
    lv_obj_t *sensRows[] = { AB.row[AB_S_TEMP], AB.row[AB_S_HUM],
                             AB.row[AB_S_BATT], AB.row[AB_S_RSSI] };
    for (unsigned i = 0; i < 4; i++) {
        if (!sensRows[i]) continue;
        if (sens) lv_obj_remove_flag(sensRows[i], LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(sensRows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (AB.sensHdr) {
        if (sens) lv_obj_remove_flag(AB.sensHdr, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(AB.sensHdr, LV_OBJ_FLAG_HIDDEN);
    }
    if (sens) {
        lv_label_set_text(AB.sensHdr,
            (s->room_src == SL2_ROOMSRC_BLE && s->room_src_status == SL2_ROOMST_OK)
                ? "SENSOR \xC2\xB7 IN CONTROL" : "SENSOR");
        if (s->ble_valid)
            lv_label_set_text_fmt(AB.val[AB_S_TEMP], "%d\xC2\xB0",
                sl2_dc_to_display(s->ble_temp_dc, s->use_f, s_caps.ftab_id));
        else lv_label_set_text(AB.val[AB_S_TEMP], AB_DASH);
        if (s->ble_valid && s->ble_hum_pct != 0xFF)
            lv_label_set_text_fmt(AB.val[AB_S_HUM], "%u%%", (unsigned)s->ble_hum_pct);
        else lv_label_set_text(AB.val[AB_S_HUM], AB_DASH);
        if (s->sensor_batt_pct >= 0)
            lv_label_set_text_fmt(AB.val[AB_S_BATT], "%d%%", s->sensor_batt_pct);
        else lv_label_set_text(AB.val[AB_S_BATT], AB_DASH);
        if (s->ble_valid) lv_label_set_text_fmt(AB.val[AB_S_RSSI], "%d dBm", s->ble_rssi);
        else              lv_label_set_text(AB.val[AB_S_RSSI], AB_DASH);
    }

    if (AB.lsensHdr) {
        lv_label_set_text(AB.lsensHdr,
            (s->room_src == SL2_ROOMSRC_LINK && s->room_src_status == SL2_ROOMST_OK)
                ? "LINK SENSOR \xC2\xB7 IN CONTROL" : "LINK SENSOR");
        if (s->link_sens_valid) {
            lv_label_set_text_fmt(AB.val[AB_LS_TEMP], "%d\xC2\xB0",
                sl2_dc_to_display(s->link_temp_dc, s->use_f, s_caps.ftab_id));
            if (s->link_hum_pct != SL2_HUM_NA)
                lv_label_set_text_fmt(AB.val[AB_LS_HUM], "%u%%", (unsigned)s->link_hum_pct);
            else lv_label_set_text(AB.val[AB_LS_HUM], AB_DASH);
        } else {
            lv_label_set_text(AB.val[AB_LS_TEMP], AB_DASH);
            lv_label_set_text(AB.val[AB_LS_HUM],  AB_DASH);
        }
    }

    if (s_link.online && (s_caps.features & SL2_FEAT_LINK_OTA_CREDS))
        lv_obj_remove_flag(AB.upd, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_obj_add_flag(AB.upd, LV_OBJ_FLAG_HIDDEN);
        if (s_ab_sel == 1) s_ab_sel = 2;
    }
    touch_floor_pair(AB.upd, &s_ab_upd_tf, AB.back, &s_ab_back_tf, TF_CAP_PILL_ROW);
    about_focus_apply();
}

void about_turn(int delta)
{
    if (delta > 0) {
        if (s_ab_sel == 0) {
            if (lv_obj_get_scroll_bottom(AB.cont) > 0)
                lv_obj_scroll_by_bounded(AB.cont, 0, -SERIN_PX(48) * delta, LV_ANIM_ON);
            else s_ab_sel = lv_obj_has_flag(AB.upd, LV_OBJ_FLAG_HIDDEN) ? 2 : 1;
        } else if (s_ab_sel == 1) s_ab_sel = 2;
    } else if (delta < 0) {
        if (s_ab_sel == 2)      s_ab_sel = lv_obj_has_flag(AB.upd, LV_OBJ_FLAG_HIDDEN) ? 0 : 1;
        else if (s_ab_sel == 1) s_ab_sel = 0;
        else lv_obj_scroll_by_bounded(AB.cont, 0, SERIN_PX(48) * -delta, LV_ANIM_ON);
    }
    about_focus_apply();
}

void about_enter(void)
{
    s_ab_sel = 0;
    about_focus_apply();
    lv_obj_scroll_to_y(AB.cont, 0, LV_ANIM_OFF);
}

void about_caps_changed(void)
{
    if (s_cur != SCREEN_ABOUT || !s_panel[SCREEN_ABOUT]) return;
    screen_rebuild(SCREEN_ABOUT);
}

void update_turn(int delta)
{
    if (!update_two_pills() || delta == 0) return;
    s_up_sel = delta > 0 ? 1 : 0;
    refresh_update();
}

void update_press(void)
{
    if (update_two_pills() && s_up_sel == 1) { notify_ota_cancel(); return; }
    ota_action();
}

void ota_action(void)
{
    switch (s_ota_st) {
        case OTA_UI_CONFIRM:  notify_ota_confirm(); break;
        case OTA_UI_CHECKING:
        case OTA_UI_DOWNLOAD: notify_ota_cancel(); break;
        case OTA_UI_UPTODATE:
        case OTA_UI_FAIL:
            s_ota_st = OTA_UI_HIDDEN;
            if (s_cur == SCREEN_UPDATE) settings_land(SET_ABOUT);
            else                         refresh_settings();
            break;
        case OTA_UI_HIDDEN:   if (s_link.online) notify_ota_check(); break;
        default: break;
    }
}

void update_hold(void)
{
    if (s_ota_st == OTA_UI_CHECKING || s_ota_st == OTA_UI_CONFIRM ||
        s_ota_st == OTA_UI_DOWNLOAD) {
        notify_ota_cancel();
    } else if (s_ota_st == OTA_UI_UPTODATE || s_ota_st == OTA_UI_FAIL) {
        ota_action();
    }
}

static void ota_btn_click_cb(lv_event_t *e) { (void)e; ota_action(); }

void about_details_cb(lv_event_t *e) { (void)e; ui_show_screen(SCREEN_ABOUT); }
