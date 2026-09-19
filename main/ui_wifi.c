/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "ui_internal.h"
#include "ui_rim.h"
#include "ui_arim.h"
#include "icons.h"
#include "sl2_proto.h"
#include "branding.h"
#include "prefs.h"
#include <stdio.h>
#include <string.h>

typedef enum { WZ_HIDDEN, WZ_STARTING, WZ_JOINQR, WZ_DONE, WZ_HK, WZ_PAIRED, WZ_QR,
               WZ_IPQR
             } wz_state_t;
#define WZ_START_TIMEOUT_S 15
#define WZ_CHANGE_QR_TIMEOUT_S 540
#define QR_TITLE_Y 78
#define WZ_IDLE_QR_TIMEOUT_S   600
static struct {
    wz_state_t st;
    lv_obj_t *title, *body, *hint;
    lv_obj_t *qr, *code;
    lv_obj_t *instr;
    lv_obj_t *icon;
    lv_obj_t *ring;
    arim_t    rim;
    lv_timer_t *tick;
    bool saw_offline;
    bool saw_ap;
    bool saw_unpaired;
    bool change;
    bool from_settings;
    bool home_entry;
    uint8_t wait_s;
    uint16_t qr_s;
} WZ;

const char *ui_wifi_face(void)
{
    switch (WZ.st) {
    case WZ_STARTING: return "starting";
    case WZ_JOINQR:   return "joinqr";
    case WZ_DONE:     return "done";
    case WZ_HK:       return "hk";
    case WZ_PAIRED:   return "paired";
    case WZ_QR:       return "hkqr";
    case WZ_IPQR:     return "ipqr";
    default:          return "hidden";
    }
}

static void wifi_wizard_back_cb(lv_event_t *e);
static void wifi_wizard_render(void);
static void wifi_ip_qr_entry(void);
void refresh_wifi(void);
static void wifi_tick_cb(lv_timer_t *t);

static bool home_pairing_supported(void)
{
    return (s_caps.features & SL2_FEAT_HOMEKIT) != 0;
}

static bool wifi_offer_pairing(void)
{
    return !WZ.change && home_pairing_supported() && s_state.hk_paired <= 0;
}

static void wifi_finish(void)
{
    if (WZ.from_settings) settings_land(SET_WIFI);
    else                  ui_show_screen(SCREEN_THERMOSTAT);
}

static void wifi_complete(void)
{
    if (!wifi_offer_pairing()) { wifi_finish(); return; }
    WZ.st = WZ_HK;
    WZ.wait_s = 0;
    WZ.qr_s = 0;
    WZ.saw_unpaired = true;
    wifi_wizard_render();
}

static void wifi_tick_arm(void)
{
    if (!WZ.tick) WZ.tick = lv_timer_create(wifi_tick_cb, 1000, NULL);
}

void wifi_setup_entry(void)
{
    if (s_link.online && s_state.wifi_connected && s_state.wifi_ip[0]) { wifi_ip_qr_entry(); return; }
    if (!s_link.online || !s_state.wifi_setup_avail) {
        settings_chip_dialog(SET_WIFI);
        return;
    }
    if (s_state.wifi_provisioned && s_state.wifi_connected) return;
    bool can_setup = (s_caps.features & SL2_FEAT_WIFI_SETUP) != 0;
    bool from_settings = s_cur == SCREEN_SETTINGS;
    ui_show_screen(SCREEN_WIFI);
    WZ.saw_offline = true;
    WZ.saw_ap = s_state.wifi_setup_ap;
    WZ.saw_unpaired = false;
    WZ.change = false;
    WZ.from_settings = from_settings;
    WZ.home_entry = false;
    WZ.wait_s = 0;
    WZ.qr_s = 0;
    if (can_setup && !s_state.wifi_setup_ap) { notify_wifi_setup(); WZ.st = WZ_STARTING; }
    else                                       WZ.st = WZ_JOINQR;
    wifi_tick_arm();
    wifi_wizard_render();
}

void wifi_change_begin(void)
{
    ui_show_screen(SCREEN_WIFI);
    WZ.saw_offline = !s_state.wifi_connected;
    WZ.saw_ap = s_state.wifi_setup_ap;
    WZ.saw_unpaired = false;
    WZ.change = true;
    WZ.from_settings = true;
    WZ.home_entry = false;
    WZ.wait_s = 0;
    WZ.qr_s = 0;
    notify_wifi_setup();
    WZ.st = s_state.wifi_setup_ap ? WZ_JOINQR : WZ_STARTING;
    wifi_tick_arm();
    wifi_wizard_render();
}

static void wifi_tick_cb(lv_timer_t *t)
{
    (void)t;
    note_activity();
    if (WZ.st == WZ_STARTING && WZ.wait_s < WZ_START_TIMEOUT_S) {
        WZ.wait_s++;
        notify_wifi_setup();
        if (WZ.wait_s == WZ_START_TIMEOUT_S)
            wifi_wizard_render();
    }
    if (WZ.st == WZ_DONE && wifi_offer_pairing() && ++WZ.wait_s >= 3) {
        wifi_complete();
        return;
    }
    if (WZ.st == WZ_JOINQR && WZ.change && ++WZ.qr_s >= WZ_CHANGE_QR_TIMEOUT_S) {
        ui_show_screen(SCREEN_THERMOSTAT);
        return;
    }
    if (((WZ.st == WZ_JOINQR && !WZ.change) || WZ.st == WZ_QR || WZ.st == WZ_HK ||
         WZ.st == WZ_PAIRED || WZ.st == WZ_IPQR || (WZ.st == WZ_DONE && !wifi_offer_pairing())) &&
        ++WZ.qr_s >= WZ_IDLE_QR_TIMEOUT_S) {
        ui_show_screen(SCREEN_THERMOSTAT);
        return;
    }
}

static void wz_icon(const serin_icon_t *ic, int px, int y_up, uint32_t col)
{
    lv_obj_set_size(WZ.icon, SERIN_PX(px), SERIN_PX(px));
    icon_fit(WZ.icon, ic, SERIN_PX(px));
    lv_obj_set_style_image_recolor(WZ.icon, lv_color_hex(col), 0);
    lv_obj_align(WZ.icon, LV_ALIGN_CENTER, 0, -SERIN_PX(y_up));
    lv_obj_remove_flag(WZ.icon, LV_OBJ_FLAG_HIDDEN);
}

static const char *hk_face_name(void)
{
    switch (hk_protocol()) {
    case HK_PROTO_MATTER:  return "Matter";
    case HK_PROTO_HOMEKIT: return "HomeKit";
    default:               return "Home";
    }
}

static void wifi_wizard_render(void)
{
    if (!s_panel[SCREEN_WIFI]) return;
    note_activity();
    lv_obj_add_flag(WZ.body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lv_obj_get_parent(WZ.qr), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(WZ.code, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(WZ.instr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(WZ.hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(WZ.icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(WZ.title, lv_color_hex(SET_COL[SET_WIFI]), 0);
    lv_obj_set_style_text_font(WZ.title, FONT_SET_NAME, 0);
    lv_obj_align(WZ.title, LV_ALIGN_TOP_MID, 0, SERIN_PX(64));
    lv_obj_align(WZ.body, LV_ALIGN_TOP_MID, 0, SERIN_PX(118));
    lv_obj_align(WZ.hint, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(40));

    bool needs_tick = (WZ.st != WZ_HIDDEN);
    if (needs_tick) wifi_tick_arm();
    if (!needs_tick && WZ.tick) { lv_timer_delete(WZ.tick); WZ.tick = NULL; }

    switch (WZ.st) {
    case WZ_STARTING:
        lv_label_set_text(WZ.title, "Set up Wi-Fi");
        lv_obj_align(WZ.title, LV_ALIGN_CENTER, 0, SERIN_PX(14));
        wz_icon(&hero_hotspot, 80, 78, SET_COL[SET_WIFI]);
        if (WZ.wait_s >= WZ_START_TIMEOUT_S) {
            lv_label_set_text(WZ.body, "The controller didn't respond.");
            lv_label_set_text(WZ.hint, H2(H_PRESS("retry"), H_HOLD("cancel")));
        } else {
            lv_label_set_text(WZ.body, "Starting the setup hotspot\xE2\x80\xA6");
            lv_label_set_text(WZ.hint, H_HOLD("cancel"));
        }
        lv_obj_align(WZ.body, LV_ALIGN_CENTER, 0, SERIN_PX(90));
        lv_obj_remove_flag(WZ.body, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(WZ.hint, LV_OBJ_FLAG_HIDDEN);
        arim_progress(WZ.ring, 40, SET_COL[SET_WIFI], true);
        break;
    case WZ_JOINQR: {
        char ssid[16];
        snprintf(ssid, sizeof(ssid), "%s-%02X%02X",
                 SERIN_AP_PREFIX, s_link.mac[4], s_link.mac[5]);
        char join[96];
        snprintf(join, sizeof(join), "WIFI:S:%s;T:WPA;P:%s;;",
                 ssid, SERIN_AP_PASSWORD);
        lv_label_set_text(WZ.title, "Set up Wi-Fi");
        lv_obj_align(WZ.title, LV_ALIGN_TOP_MID, 0, SERIN_PX(QR_TITLE_Y));
        lv_qrcode_update(WZ.qr, join, (uint32_t)strlen(join));
        lv_obj_remove_flag(lv_obj_get_parent(WZ.qr), LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(WZ.code, ssid);
        lv_obj_set_style_text_font(WZ.code, FONT_UNITS, 0);
        lv_obj_align(WZ.code, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(96));
        lv_obj_remove_flag(WZ.code, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(WZ.instr, "Scan to set up Wi-Fi");
        lv_obj_remove_flag(WZ.instr, LV_OBJ_FLAG_HIDDEN);
        if (WZ.change) {
            lv_obj_align(WZ.hint, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(28));
            lv_label_set_text(WZ.hint, H_PRESS("cancel"));
            lv_obj_remove_flag(WZ.hint, LV_OBJ_FLAG_HIDDEN);
        }
        arim_progress(WZ.ring, ARIM_SPAN_DEG / 2, SET_COL[SET_WIFI], true);
        break;
    }
    case WZ_DONE:
        lv_label_set_text(WZ.title, "Wi-Fi connected");
        lv_obj_set_style_text_color(WZ.title, lv_color_hex(COL_GREEN), 0);
        lv_obj_align(WZ.title, LV_ALIGN_CENTER, 0, SERIN_PX(30));
        wz_icon(&hero_check, 104, 64, COL_GREEN);
        if (WZ.change) {
            lv_label_set_text(WZ.body, "Network updated");
            lv_obj_align(WZ.body, LV_ALIGN_CENTER, 0, SERIN_PX(86));
            lv_obj_remove_flag(WZ.body, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(WZ.hint, H_PRESS("finish"));
            lv_obj_remove_flag(WZ.hint, LV_OBJ_FLAG_HIDDEN);
        }
        if (!wifi_offer_pairing()) {
            lv_label_set_text(WZ.hint, H_PRESS("finish"));
            lv_obj_remove_flag(WZ.hint, LV_OBJ_FLAG_HIDDEN);
        }
        arim_progress(WZ.ring, ARIM_SPAN_DEG, COL_GREEN, true);
        break;
    case WZ_HK:
    case WZ_QR: {
        const hvac_state_t *s = &s_state;
        lv_label_set_text(WZ.title, hk_face_name());
        lv_obj_set_style_text_color(WZ.title, lv_color_hex(SET_COL[SET_HOMEKIT]), 0);
        lv_obj_set_style_text_font(WZ.code, FONT_HK_CODE, 0);
        if (s->hk_payload[0]) {
            lv_obj_align(WZ.title, LV_ALIGN_TOP_MID, 0, SERIN_PX(QR_TITLE_Y));
            lv_qrcode_update(WZ.qr, s->hk_payload, (uint32_t)strlen(s->hk_payload));
            lv_obj_remove_flag(lv_obj_get_parent(WZ.qr), LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(WZ.code, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(84));
        } else {
            lv_label_set_text(WZ.body, s->wifi_setup_avail
                ? "Waiting for the Serin controller\xE2\x80\xA6"
                : (hk_protocol() == HK_PROTO_HOMEKIT
                    ? "Enter this code in the Home app."
                    : "Enter this code in your home app."));
            lv_obj_align(WZ.body, LV_ALIGN_TOP_MID, 0, SERIN_PX(118));
            lv_obj_remove_flag(WZ.body, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(WZ.code, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(WZ.code, s->hk_code[0] ? s->hk_code : "---");
        arim_progress(WZ.ring, ARIM_SPAN_DEG / 2, SET_COL[SET_HOMEKIT], true);
        break;
    }
    case WZ_IPQR: {
        const hvac_state_t *s = &s_state;
        char url[32];
        snprintf(url, sizeof url, "http://%.15s/", s->wifi_ip);
        lv_label_set_text(WZ.title, "Controller");
        lv_obj_align(WZ.title, LV_ALIGN_TOP_MID, 0, SERIN_PX(QR_TITLE_Y));
        lv_qrcode_update(WZ.qr, url, (uint32_t)strlen(url));
        lv_obj_remove_flag(lv_obj_get_parent(WZ.qr), LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(WZ.code, FONT_UNITS, 0);
        lv_obj_align(WZ.code, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(96));
        lv_label_set_text(WZ.code, s->wifi_ip);
        lv_obj_remove_flag(WZ.code, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(WZ.instr, "Scan to open");
        lv_obj_align(WZ.instr, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(60));
        lv_obj_remove_flag(WZ.instr, LV_OBJ_FLAG_HIDDEN);
        arim_progress(WZ.ring, ARIM_SPAN_DEG, SET_COL[SET_WIFI], true);
        break;
    }
    case WZ_PAIRED:
        lv_label_set_text_fmt(WZ.title, "%s\npaired", hk_face_name());
        lv_obj_set_style_text_color(WZ.title, lv_color_hex(SET_COL[SET_HOMEKIT]), 0);
        lv_obj_align(WZ.title, LV_ALIGN_CENTER, 0, SERIN_PX(44));
        wz_icon(&hero_check, 104, 84, SET_COL[SET_HOMEKIT]);
        lv_label_set_text(WZ.hint, H_PRESS("finish"));
        lv_obj_remove_flag(WZ.hint, LV_OBJ_FLAG_HIDDEN);
        arim_progress(WZ.ring, ARIM_SPAN_DEG, SET_COL[SET_HOMEKIT], true);
        break;
    default: break;
    }
}

void wifi_encoder(int d) { (void)d; }

void wifi_press(void)
{
    switch (WZ.st) {
    case WZ_STARTING:
        if (WZ.wait_s >= WZ_START_TIMEOUT_S) {
            WZ.wait_s = 0;
            notify_wifi_setup();
            wifi_wizard_render();
        }
        break;
    case WZ_JOINQR:
        if (WZ.change) wifi_wizard_back_cb(NULL);
        else           wifi_finish();
        break;
    case WZ_DONE:
        wifi_complete();
        break;
    case WZ_HK:
        wifi_finish();
        break;
    case WZ_QR:
    case WZ_IPQR:
        wifi_wizard_back_cb(NULL);
        break;
    case WZ_PAIRED:
        wifi_wizard_back_cb(NULL);
        break;
    default: break;
    }
}

void wifi_back(void)
{
    if (WZ.st == WZ_JOINQR && !WZ.change) { wifi_finish(); return; }
    if (WZ.st == WZ_HK) { wifi_finish(); return; }
    if (WZ.st == WZ_PAIRED) { wifi_press(); return; }
    wifi_wizard_back_cb(NULL);
}

void build_wifi(void)
{
    lv_obj_t *p = s_panel[SCREEN_WIFI] = mkpanel(COL_FACE, true);

    WZ.title = mklabel(p, FONT_SET_NAME, SET_COL[SET_WIFI], "Wi-Fi");
    lv_obj_set_style_text_align(WZ.title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(WZ.title, LV_ALIGN_TOP_MID, 0, SERIN_PX(64));

    WZ.body = mkbody(p, FONT_WZ_BODY, COL_DIM, "", SERIN_PX(300));
    lv_obj_align(WZ.body, LV_ALIGN_TOP_MID, 0, SERIN_PX(118));

    lv_obj_t *pad = lv_obj_create(p);
    lv_obj_remove_style_all(pad);
    lv_obj_set_size(pad, SERIN_PX(192), SERIN_PX(192));
    lv_obj_set_style_radius(pad, SERIN_PX(12), 0);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(pad, lv_color_white(), 0);
    lv_obj_align(pad, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(pad, LV_OBJ_FLAG_CLICKABLE);
    WZ.qr = lv_qrcode_create(pad);
    lv_qrcode_set_size(WZ.qr, SERIN_PX(168));
    lv_qrcode_set_dark_color(WZ.qr, lv_color_black());
    lv_qrcode_set_light_color(WZ.qr, lv_color_white());
    lv_obj_center(WZ.qr);

    WZ.code = mklabel(p, FONT_HK_CODE, COL_WHITE, "");
    lv_obj_align(WZ.code, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(96));

    WZ.hint = mklabel(p, FONT_WZ_HINT, COL_GREY, "");
    lv_obj_set_style_text_align(WZ.hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(WZ.hint, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(40));

    WZ.instr = mklabel(p, FONT_WZ_BODY, COL_DIM, "");
    lv_obj_align(WZ.instr, LV_ALIGN_BOTTOM_MID, 0, -SERIN_PX(60));
    lv_obj_add_flag(WZ.instr, LV_OBJ_FLAG_HIDDEN);

    WZ.icon = mkicon(p, &hero_check, SERIN_PX(104), COL_GREEN);
    lv_obj_align(WZ.icon, LV_ALIGN_CENTER, 0, -SERIN_PX(64));
    lv_obj_add_flag(WZ.icon, LV_OBJ_FLAG_HIDDEN);

    WZ.ring = arim_ring_create(p, &WZ.rim);
}

void wifi_destroy(void)
{
    if (WZ.tick) { lv_timer_delete(WZ.tick); WZ.tick = NULL; }
    memset(&WZ, 0, sizeof(WZ));
}

void refresh_wifi(void)
{
    if (!s_panel[SCREEN_WIFI] || s_cur != SCREEN_WIFI) return;
    if ((WZ.st == WZ_HK || WZ.st == WZ_QR) && !home_pairing_supported()) {
        settings_land(SET_WIFI);
        return;
    }
    if (WZ.st == WZ_DONE) {
        wifi_wizard_render();
        return;
    }
    if (!s_state.wifi_connected || s_state.alert == ALERT_NOLINK)
        WZ.saw_offline = true;
    if (s_state.wifi_setup_ap) WZ.saw_ap = true;
    if (WZ.st == WZ_STARTING) {
        if (!s_state.wifi_setup_ap) return;
        WZ.st = WZ_JOINQR;
        wifi_wizard_render();
        return;
    }
    if (WZ.st == WZ_JOINQR) {
        if (s_state.alert == ALERT_NOLINK || s_state.alert == ALERT_SYNCING) return;
        bool edge    = !WZ.change && s_state.wifi_connected && WZ.saw_offline;
        bool ap_done = s_state.wifi_connected && WZ.saw_ap && !s_state.wifi_setup_ap;
        if (!edge && !ap_done) return;
        WZ.wait_s = 0;
        WZ.qr_s = 0;
        WZ.st = WZ_DONE;
        wifi_wizard_render();
        return;
    }
    if (WZ.st == WZ_HK || WZ.st == WZ_QR) {
        if (s_state.alert == ALERT_NOLINK || s_state.alert == ALERT_SYNCING) return;
        if (s_state.hk_paired <= 0) {
            WZ.saw_unpaired = true;
        } else if (WZ.saw_unpaired) {
            WZ.qr_s = 0;
            WZ.st = WZ_PAIRED;
            wifi_wizard_render();
            return;
        }
        wifi_wizard_render();
    }
}

/* Leaving the wizard goes back where it was opened from.  The wizard is a
 * transient screen and so never joins the back trail, which is why the entry
 * point is remembered here instead. */
static void wifi_wizard_back_cb(lv_event_t *e)
{
    (void)e;
    if (!WZ.from_settings) { ui_show_screen(SCREEN_THERMOSTAT); return; }
    settings_land(WZ.home_entry ? SET_HOMEKIT : SET_WIFI);
}

void homekit_qr_entry(void)
{
    if (!home_pairing_supported()) return;
    if (!s_link.online) { settings_chip_dialog(SET_HOMEKIT); return; }
    ui_show_screen(SCREEN_WIFI);
    WZ.st = WZ_QR;
    WZ.from_settings = true;
    WZ.home_entry = true;
    WZ.saw_unpaired = (s_state.hk_paired <= 0);
    wifi_tick_arm();
    wifi_wizard_render();
}

static void wifi_ip_qr_entry(void)
{
    if (!s_link.online || !s_state.wifi_connected || !s_state.wifi_ip[0]) {
        settings_chip_dialog(SET_WIFI);
        return;
    }
    ui_show_screen(SCREEN_WIFI);
    WZ.st = WZ_IPQR;
    WZ.from_settings = true;
    WZ.home_entry = false;
    WZ.qr_s = 0;
    wifi_tick_arm();
    wifi_wizard_render();
}
