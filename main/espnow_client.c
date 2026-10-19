/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "espnow_client.h"
#include <stddef.h>
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <esp_random.h>
#include "wifi_setup_session.h"
#include "room_replay.h"
#include <esp_mac.h>
#include <esp_console.h>
#include <linenoise/linenoise.h>
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "ui.h"
#include "link_ota.h"
#include "sl2_proto.h"
#include "sl2_pair_auth.h"
#include "sl2_ui_map.h"
#include "sl2_info_apply.h"
#include <esp_app_desc.h>
#include "serin_board.h"
#include "ui_dd.h"
#include "espnow_bond.h"
#include "espnow_liveness.h"
#include "espnow_chscan.h"
#include "espnow_events.h"
#include "edit_hold.h"
#include "link_crypto.h"
#include "wdt_crumb.h"
#include "ota_ver.h"
#include "prefs.h"
#include "link_ota.h"
#include "sensor_sht40.h"
#include "bsp.h"

static const char *TAG = "espnow_client";

static bool     s_use_f = true;
static uint8_t  s_channel;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static inline uint32_t ms_stamp(uint32_t now) { return now ? now : 1; }

typedef struct {
    espnow_bond_rec_t bond;
    hvac_state_t st;
    bool     synced;
    volatile uint16_t epoch;
    uint16_t epochSync;
    volatile uint32_t lastRxMs;
    volatile uint32_t lastStateRxMs; /* Actual receipt; never advanced for hold grace. */
    volatile uint8_t  lastStateCh;
    volatile uint32_t lastCapsRxMs;
    volatile uint8_t  lastCapsCh;
    volatile uint32_t pendAckMs;
    volatile uint8_t  pendAckCh;
    volatile uint32_t lastAckMs;
    volatile uint8_t  lastAckCh;
    volatile uint32_t ackWin;
    volatile int      rssi;
    volatile uint8_t  peerVer;
    uint32_t lastProbeMs;
    uint32_t lastHopProbeMs;
    uint32_t firstProbeMs;
    volatile uint8_t chMiss;
    uint8_t  scanIdx;
    uint32_t scanRestMs;
    uint32_t infoTxMs;
    bool     infoTxOk;
    uint8_t  infoCapsSeq;
    volatile uint32_t sensTxMs;
    int16_t  sensTxDc;
    uint64_t roomSourceId;
    uint8_t  roomSourceStatus;
    uint8_t  roomRequestId;
    wifi_setup_session_t wifiSession;
    uint8_t  gate;
    uint8_t  nightCeil;
    bool     night;
    bool     scrTxValid;
    bool     scrTxOn;
    volatile uint8_t  caps_seq;
    volatile bool     want_caps;
    bool     vaneWarned;
    ui_caps_t caps;
} zone_t;
static const ui_caps_t ZONE_CAPS_DEFAULT = {
    .modes = 0x0077  , .features = 0x00FF,
    .fan_steps = 5, .fan_has_auto = true,
    .vane_v = SL2_VANECAP(5, true, true),
    .vane_h = SL2_VANECAP(6, false, true) | SL2_VANECAP_SPLIT_TOP,
    .set_min_dc = 160, .set_max_dc = 305, .set_step_dc = 5, .ftab_id = 1,
    .band_min_gap_dc = 5,
};
static zone_t s_z[ESPNOW_MAX_BONDS];
static int          s_zn;
static volatile int s_active;
static volatile int s_zone_req = -1;
/* Published by link_task after applying the selected bonded controller's
 * STATE. OTA reads this single 32-bit snapshot, not mutable zone/UI fields.
 * Zero means no applied STATE for the selection; hold grace never renews it. */
static _Atomic uint32_t s_ota_state_rx_ms;

static struct sl2_room_source_entry s_catalog[SL2_ROOM_CATALOG_MAX];
static uint8_t  s_catalog_count;
static uint32_t s_catalog_revision;
static int      s_catalog_zi = -1;
static struct sl2_room_source_entry s_catalog_stage[SL2_ROOM_CATALOG_MAX];
static uint8_t  s_catalog_stage_count;
static uint32_t s_catalog_stage_revision;
static int      s_catalog_stage_zi = -1;

static bool catalog_held(int zi) { return s_catalog_zi == zi && s_catalog_count; }

static void catalog_reset(void)
{
    s_catalog_count = 0; s_catalog_revision = 0; s_catalog_zi = -1;
    s_catalog_stage_count = 0; s_catalog_stage_revision = 0; s_catalog_stage_zi = -1;
}

typedef char ui_zones_sa[(UI_MAX_ZONES == ESPNOW_MAX_BONDS) ? 1 : -1];
_Static_assert(ESPNOW_MAX_BONDS <= CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM,
               "more bonds than the driver will hold as encrypted peers");

#define BG_PROBE_MS      4000
#define ZONES_OPEN_PROBE_MS 1000
#define ZONE_HOP_INTERVAL_MS  250

static int zone_by_mac(const uint8_t mac[6]) {
    for (int i = 0; i < s_zn; i++)
        if (memcmp(s_z[i].bond.mac, mac, 6) == 0) return i;
    return -1;
}
static bool zone_cross_channel(int i) {
    return s_channel && s_z[i].bond.last_channel &&
           s_z[i].bond.last_channel != s_channel;
}
static bool zone_acked(int i) {
    return espnow_ack_fresh(now_ms(), s_z[i].lastAckMs, s_z[i].ackWin);
}
static bool zone_online(int i) {
    return espnow_link_fresh(now_ms(), s_z[i].lastAckMs, s_z[i].ackWin,
                            s_z[i].lastStateRxMs);
}
static bool zone_rx_stale(int i) {
    return espnow_rx_stale(now_ms(), s_z[i].lastRxMs, s_z[i].synced);
}

static void zone_ch_miss(int i) {
    if (s_z[i].chMiss < 255 && ++s_z[i].chMiss == ESPNOW_CHSCAN_MISS_N) {
        s_z[i].scanIdx = 0; s_z[i].scanRestMs = 0;
        ESP_LOGI(TAG, "zone %d ch %u unanswered x%d — channel scan armed",
                 i, s_z[i].bond.last_channel, ESPNOW_CHSCAN_MISS_N);
    }
}

static int bond_channels(int exclude, uint8_t sib[ESPNOW_MAX_BONDS]) {
    int ns = 0;
    for (int j = 0; j < s_zn; j++)
        if (j != exclude) sib[ns++] = s_z[j].bond.last_channel;
    return ns;
}

static uint8_t zone_scan_next(int i, uint32_t now) {
    zone_t *z = &s_z[i];
    if (z->scanRestMs &&
        (uint32_t)(now - z->scanRestMs) < ESPNOW_CHSCAN_ROUND_REST_MS)
        return 0;
    uint8_t sib[ESPNOW_MAX_BONDS];
    int ns = bond_channels(i, sib);
    uint8_t ch = espnow_chscan_channel(z->scanIdx, s_channel,
                                       z->bond.last_channel, sib, ns);
    if (!ch) { z->scanIdx = 0; z->scanRestMs = ms_stamp(now); return 0; }
    z->scanIdx++;
    return ch;
}

static void zones_init(void) {
    static bool done;
    if (done) return;
    done = true;
    espnow_bond_rec_t recs[ESPNOW_MAX_BONDS];
    s_zn = espnow_bonds_load(recs);
    for (int i = 0; i < s_zn; i++) {
        s_z[i].bond = recs[i];
        s_z[i].want_caps = true;
        s_z[i].caps = ZONE_CAPS_DEFAULT;
        s_z[i].st.use_f = s_use_f;
    }
    int a = prefs_get_u8("activeZone", 0);
    s_active = (a >= 0 && a < s_zn) ? a : 0;
}

static bool espnow_client_online(void) { return s_zn > 0 && zone_online(s_active); }
void espnow_client_refresh_idents(void) {
    for (int i = 0; i < s_zn; i++) s_z[i].want_caps = true;
}
void espnow_client_set_active(int idx) {
    if (idx < 0 || idx >= s_zn || idx == s_active) return;
    s_zone_req = idx;
}

static void stamp_first_probe(int i, uint32_t now) {
    if (!s_z[i].firstProbeMs) s_z[i].firstProbeMs = ms_stamp(now);
}

static ui_zone_badge_t zone_badge(int i) {
    zone_t *z = &s_z[i];
    bool checking = espnow_zone_checking(now_ms(), z->firstProbeMs,
                                         z->lastAckMs, z->synced);
    bool live     = zone_online(i) && !zone_rx_stale(i);
    bool fault    = z->synced && z->st.alert == ALERT_FAULT;
    return zone_badge_decide(checking, live, fault, zone_cross_channel(i));
}
static const char *const s_badge_name[] = { "OFFLINE", "STALE", "ONLINE", "FAULT",
                                            "CHECKING" };
static void push_zones_ui_locked(void) {
    static uint8_t prev[ESPNOW_MAX_BONDS] = { [0 ... ESPNOW_MAX_BONDS-1] = 0xFF };
    ui_zone_info_t zi[ESPNOW_MAX_BONDS];
    for (int i = 0; i < s_zn; i++) {
        memcpy(zi[i].name, s_z[i].bond.name, sizeof zi[i].name);
        memcpy(zi[i].mac, s_z[i].bond.mac, 6);
        zi[i].have_state = s_z[i].synced;
        zi[i].room_temp  = s_z[i].st.room_temp;
        zi[i].use_f      = s_z[i].st.use_f;
        zi[i].mode       = s_z[i].st.mode;
        zi[i].status     = s_z[i].st.status;
        zi[i].set_dd     = s_z[i].st.set_dd;
        zi[i].set_lo_dd  = s_z[i].st.set_lo_dd;
        zi[i].set_hi_dd  = s_z[i].st.set_hi_dd;
        zi[i].band_valid = s_z[i].st.band_valid;
        zi[i].badge      = zone_badge(i);
        if ((uint8_t)zi[i].badge != prev[i]) {
            uint32_t a = s_z[i].lastAckMs;
            uint32_t r = s_z[i].lastRxMs;
            ESP_LOGI(TAG, "zone %d badge %s -> %s (ack %lu ms ago, rx %lu ms ago, synced %d, zone ch %u, radio ch %u)",
                     i, prev[i] <= UI_ZB_CHECKING ? s_badge_name[prev[i]] : "?",
                     s_badge_name[zi[i].badge],
                     a ? (unsigned long)(now_ms() - a) : 0UL,
                     r ? (unsigned long)(now_ms() - r) : 0UL,
                     s_z[i].synced,
                     s_z[i].bond.last_channel, s_channel);
            prev[i] = (uint8_t)zi[i].badge;
        }
    }
    ui_set_zones(zi, s_zn, s_active);
}
static void push_zones_ui(void) {
    if (!lvgl_port_lock(0)) return;
    push_zones_ui_locked();
    lvgl_port_unlock();
}

static void badge_paint_if_changed(uint32_t *sig) {
    uint32_t s = 0;
    for (int i = 0; i < s_zn; i++) s = (s << 3) | (uint32_t)zone_badge(i);
    if (s == *sig) return;
    *sig = s;
    push_zones_ui();
}

static hvac_state_t s_last_sent;
/* What the controller itself reports for the held fields. STATE handling
 * masks incoming values through the holds, so s_last_sent and the zone copy
 * carry an in-flight overshoot; these do not. */
static int s_ctrl_set_dd, s_ctrl_lo_dd, s_ctrl_hi_dd, s_ctrl_fan;
static void ctrl_from(const hvac_state_t *st) {
    s_ctrl_set_dd = st->set_dd;
    s_ctrl_lo_dd = st->set_lo_dd; s_ctrl_hi_dd = st->set_hi_dd;
    s_ctrl_fan = st->fan_pct;
}

static edit_hold_t  s_set_hold;
static bool     s_set_hold_use_f = false;
static int      s_tx_sent_temp = -1000;
static edit_hold2_t s_band_hold;
static bool     s_band_hold_use_f = false;
static int      s_tx_sent_lo = -1000, s_tx_sent_hi = -1000;
static edit_hold_t  s_unit_hold;
static edit_hold_t  s_mode_hold;
static edit_hold_t  s_vv_hold;
static edit_hold_t  s_hv_hold;
static edit_hold_t  s_fan_hold;
static int      s_tx_sent_fan = -1000;
#define SET_HOLD_MS 3000
#define EDIT_DEBOUNCE_MS 1000
#define FAN_DEBOUNCE_MS 300
#define SYNC_WAIT_MS 12000
static uint32_t s_sync_start_ms;

static void sweep_backoff_reset(void);
static bool ensure_radio(void);
static volatile bool s_link_task_up;

static volatile bool s_pairing = false;
static volatile bool s_have_resp = false;
static volatile bool s_pair_cancel = false;
static struct sl2_pair_resp_pkt s_resp;
static uint8_t s_own_mac[6];
static uint8_t s_pair_priv[32], s_pair_pub[32];
static bool    s_radio_up = false;
static TaskHandle_t s_pair_task = NULL;
static SemaphoreHandle_t s_pair_go = NULL;
static volatile bool s_pair_busy = false;

static uint8_t  s_pair_mac[6];
static volatile bool s_pair_confirm = false;
static volatile uint32_t s_pair_ack_ms = 0;
static struct sl2_pair_auth_pkt s_pair_expected_ack;
static portMUX_TYPE s_pair_mux = portMUX_INITIALIZER_UNLOCKED;

static void pair_confirm_stop(void) {
    portENTER_CRITICAL(&s_pair_mux);
    s_pair_confirm = false;
    memset(&s_pair_expected_ack, 0, sizeof s_pair_expected_ack);
    portEXIT_CRITICAL(&s_pair_mux);
}

static volatile bool s_ota_hold = false;
static void radio_ps_apply(void);
static bool s_ps_applied_min;
/* Keep the physical radio awake for asynchronous cancellation replies.
 * WIFI_PS_NONE alone does not override ESP-NOW's connectionless RX window.
 * Retries renew this bounded hold; timeout/exit always releases the reference. */
#define WIFI_CANCEL_REPLY_MS 2000u
static bool s_wifi_cancel_rx_hold;
static uint32_t s_wifi_cancel_tx_ms;
static int  cmd_radio_ps(int argc, char **argv);
static volatile bool s_wifi_resp_have = false;
static volatile bool s_wifi_req_pending = false;
static struct sl2_wifi_resp_pkt s_wifi_resp;

static void alert_set(hvac_alert_t a) {
    hvac_state_t s; ui_get_state(&s); s.alert = a; ui_set_state(&s);
}

/* Owned by link_task, including apply_state() and zone switching. */
static espnow_offline_alert_t s_offline_alert;
/* A brief hold can begin and end between link_task iterations. */
static atomic_bool s_offline_alert_reset_due;

static void offline_alert_poll(void) {
    if (atomic_exchange(&s_offline_alert_reset_due, false))
        espnow_offline_alert_reset(&s_offline_alert);
    espnow_offline_alert_note_reply(&s_offline_alert, s_z[s_active].lastStateRxMs);
    if (!espnow_offline_alert_due(&s_offline_alert, now_ms())) return;
    if (!lvgl_port_lock(0)) return;
    alert_set(ALERT_NOLINK);
    s_offline_alert.shown = true;
    lvgl_port_unlock();
}

#define TX_ACK_MS 100
static SemaphoreHandle_t s_tx_mux;
static SemaphoreHandle_t s_tx_done;
static volatile bool     s_tx_ok;
static int               s_tx_owed;

typedef enum {
    LINK_TX_ACKED,
    LINK_TX_NO_ACK,
    LINK_TX_NO_MEM,
    LINK_TX_REFUSED,
} link_tx_t;

static link_tx_t link_send_ex(const uint8_t mac[6], const void *buf, size_t len,
                              const char *what) {
    if (!s_tx_mux || !s_tx_done) return LINK_TX_REFUSED;
    xSemaphoreTake(s_tx_mux, portMAX_DELAY);
    while (s_tx_owed > 0) {
        if (xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(TX_ACK_MS)) != pdTRUE)
            break;
        s_tx_owed--;
    }
    xSemaphoreTake(s_tx_done, 0);
    s_tx_ok = false;
    esp_err_t e = esp_now_send(mac, (const uint8_t *)buf, len);
    bool acked = false;
    if (e == ESP_OK) {
        if (xSemaphoreTake(s_tx_done, pdMS_TO_TICKS(TX_ACK_MS)) == pdTRUE) acked = s_tx_ok;
        else s_tx_owed++;
    }
    xSemaphoreGive(s_tx_mux);
    if (e != ESP_OK) {
        static uint32_t lastWarn;
        uint32_t now = now_ms();
        if (lastWarn == 0 || now - lastWarn >= 5000) {
            lastWarn = now;
            ESP_LOGW(TAG, "%s -> %02X:%02X:%02X:%02X:%02X:%02X refused by the driver: %s",
                     what, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                     esp_err_to_name(e));
        }
    }
    if (acked)   return LINK_TX_ACKED;
    if (e == ESP_OK) return LINK_TX_NO_ACK;
    if (e == ESP_ERR_ESPNOW_NO_MEM) return LINK_TX_NO_MEM;
    if (e == ESP_ERR_ESPNOW_NOT_FOUND || e == ESP_ERR_ESPNOW_ARG ||
        e == ESP_ERR_ESPNOW_IF        || e == ESP_ERR_ESPNOW_CHAN ||
        e == ESP_ERR_ESPNOW_NOT_INIT)
        return LINK_TX_REFUSED;
    return LINK_TX_NO_ACK;
}

static bool link_send(const uint8_t mac[6], const void *buf, size_t len,
                      const char *what) {
    return link_send_ex(mac, buf, len, what) == LINK_TX_ACKED;
}

#define CMD_TRIES 3
static void cmd_tx(int zi, const struct sl2_cmd_pkt *cin) {
    if (zi < 0 || zi >= s_zn) return;
    struct sl2_cmd_pkt c = *cin;
    c.epoch = s_z[zi].epoch;
    s_z[zi].epochSync = c.epoch;
    for (int t = 1; t <= CMD_TRIES; t++) {
        link_tx_t r = link_send_ex(s_z[zi].bond.mac, &c, sizeof c, "CMD");
        if (r == LINK_TX_ACKED) return;
        if (r == LINK_TX_REFUSED) {
            return;
        }
        if (t == CMD_TRIES) break;
        ESP_LOGW(TAG, "CMD (mask 0x%04X) unACKed by zone %d — retransmit %d/%d",
                 c.mask, zi, t, CMD_TRIES - 1);
        vTaskDelay(pdMS_TO_TICKS(r == LINK_TX_NO_MEM ? 80 : 20));
    }
    ESP_LOGW(TAG, "CMD (mask 0x%04X) lost — the unit will re-assert its own state",
             c.mask);
}

static volatile uint8_t s_radio_ch;
static bool radio_set_channel(uint8_t ch) {
    if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    s_radio_ch = ch;
    return true;
}

static void radio_go_home(void) {
    if (s_channel) radio_set_channel(s_channel);
}

static void note_delivery(int i, uint8_t ch) {
    const uint32_t now = now_ms();
    if (espnow_ack_corroborated(now, s_z[i].pendAckMs, s_z[i].pendAckCh, ch)) {
        s_z[i].ackWin = espnow_ack_window(i == s_active);
        s_z[i].lastAckMs = ms_stamp(now);
        s_z[i].chMiss = 0;
        if (ch)
            s_z[i].lastAckCh = ch;
    }
    s_z[i].pendAckMs = ms_stamp(now);
    s_z[i].pendAckCh = ch;
}

static void on_send(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
    const bool ok = (status == ESP_NOW_SEND_SUCCESS);
    const int  i  = ok ? zone_by_mac(tx_info->des_addr) : -1;
    if (i >= 0) note_delivery(i, s_radio_ch);
    s_tx_ok = ok;
    if (s_tx_done) xSemaphoreGive(s_tx_done);
}

static void vane_range_check(int zi, const struct sl2_state_pkt *p) {
    zone_t *z = &s_z[zi];
    if (z->vaneWarned) return;
    if (!sl2_ui_vane_clamps(p->vane_v, 0) &&
        !sl2_ui_vane_clamps(p->vane_h, sl2_vanecap_split_pos(z->caps.vane_h)))
        return;
    z->vaneWarned = true;
    ESP_LOGW(TAG, "zone %d reports a vane position past the %d the dial draws "
                  "(v=%u h=%u; caps v=0x%02X h=0x%02X) — clamped",
             zi, SL2_UI_VANE_MAX_POS, p->vane_v, p->vane_h,
             z->caps.vane_v, z->caps.vane_h);
}

static void resend_held_edits(int zi);

static ui_screen_gate_t s_gate_applied = UI_SCREEN_GATE_NONE;
static void screen_gate_sync(void) {
    ui_screen_gate_t g = UI_SCREEN_GATE_NONE;
    for (int i = 0; i < s_zn; i++)
        if (zone_online(i))
            g = sl2_screen_gate_merge(g, (ui_screen_gate_t)s_z[i].gate);
    if (g == s_gate_applied) return;
    if (!lvgl_port_lock(0)) return;
    s_gate_applied = g;
    ui_set_screen_gate(g);
    lvgl_port_unlock();
}

static bool    s_night_applied      = false;
static uint8_t s_night_ceil_applied = 0;
static void night_sync(void) {
    bool n = false;
    uint8_t ceil = 0;
    for (int i = 0; i < s_zn; i++) {
        if (!zone_online(i)) continue;
        if (s_z[i].night) n = true;
        uint8_t c = s_z[i].nightCeil;
        if (c && (!ceil || c < ceil)) ceil = c;
    }
    if (n == s_night_applied && ceil == s_night_ceil_applied) return;
    if (!lvgl_port_lock(0)) return;
    s_night_applied      = n;
    s_night_ceil_applied = ceil;
    ui_set_night(n, ceil);
    lvgl_port_unlock();
}

static void apply_state(int zi, const struct sl2_state_pkt *p, uint32_t received_ms) {
    zone_t *z = &s_z[zi];
    if (zi == s_active) sweep_backoff_reset();
    bool epoch_moved = z->epochSync && p->epoch && p->epoch != z->epochSync;
    if (epoch_moved) z->epochSync = p->epoch;
    if (z->epoch != p->epoch) {
        /* Fresh live readings must resume immediately after resync. */
        z->sensTxMs = 0;
        z->scrTxValid = false;
    }
    z->epoch = p->epoch;
    bool use_f = (p->flags & SL2_SF_USE_F) != 0;
    if (z->caps_seq != p->caps_seq) z->want_caps = true;
    z->gate = (uint8_t)sl2_screen_gate_from_flags2(p->flags2);
    z->night = sl2_night_from_flags2(p->flags2);
    z->nightCeil = (p->flags2 & SL2_SF2_NIGHT_CTL) ? p->night_ceil : 0;
    vane_range_check(zi, p);
    if (!lvgl_port_lock(0)) return;
    bool is_active = (zi == s_active);
    uint32_t now = now_ms();
    if (is_active)
        use_f = edit_hold_echo(&s_unit_hold, use_f ? 1 : 0, now, SET_HOLD_MS) != 0;
    hvac_state_t s;
    if (is_active) ui_get_state(&s); else s = z->st;
    s.mode       = mode_from_sl2(p->mode);
    if (is_active)
        s.mode = (hvac_mode_t)edit_hold_echo(&s_mode_hold, (int)s.mode, now, SET_HOLD_MS);
    s.status     = (s.mode == MODE_OFF) ? ST_OFF
                 : (action_active(p->action) ? ST_ACTIVE : ST_IDLE);
    s.room_temp  = sl2_dc_to_display(p->room_dc, use_f, z->caps.ftab_id);
    if (p->set_dc != SL2_DC_NA) {
        int set_echo = ui_dd_from_dc(p->set_dc, use_f, z->caps.ftab_id);
        if (is_active) {
            s_ctrl_set_dd = set_echo;
            set_echo = edit_hold_echo(&s_set_hold, set_echo, now, SET_HOLD_MS);
        }
        s.set_dd = set_echo;
    }
    s.band_valid = (p->set_low_dc != SL2_DC_NA && p->set_high_dc != SL2_DC_NA);
    if (s.band_valid) {
        int lo = ui_dd_from_dc(p->set_low_dc,  use_f, z->caps.ftab_id);
        int hi = ui_dd_from_dc(p->set_high_dc, use_f, z->caps.ftab_id);
        if (is_active) {
            s_ctrl_lo_dd = lo; s_ctrl_hi_dd = hi;
            edit_hold2_echo(&s_band_hold, &lo, &hi, now, SET_HOLD_MS);
        }
        s.set_lo_dd = lo; s.set_hi_dd = hi;
    }
    s.fan_pct    = p->fan > 100 ? 100 : p->fan;
    s.vvane      = vvane_from_sl2(p->vane_v);
    s.hvane      = hvane_from_sl2(p->vane_h, z->caps.vane_h);
    if (is_active) {
        s_ctrl_fan = s.fan_pct;
        s.fan_pct = edit_hold_echo(&s_fan_hold, s.fan_pct, now, SET_HOLD_MS);
        s.vvane   = (vvane_t)edit_hold_echo(&s_vv_hold, (int)s.vvane, now, SET_HOLD_MS);
        s.hvane   = (hvane_t)edit_hold_echo(&s_hv_hold, (int)s.hvane, now, SET_HOLD_MS);
    }
    s.use_f      = use_f;
    s.humidity_valid = (p->room_hum_pct != SL2_HUM_NA);
    if (s.humidity_valid) s.humidity = p->room_hum_pct;
    s.error_code = p->fault;
    s.alert      = p->fault ? ALERT_FAULT : ALERT_NONE;
    s.cn105_connected = (p->flags & SL2_SF_HVAC_LINK) != 0;
    s.sensor_batt_low = (p->flags2 & SL2_SF2_SENSOR_BATT_LOW) != 0;
    s.wifi_connected = (p->flags & SL2_SF_WIFI) != 0;
    s.wifi_setup_avail = true;
    s.wifi_provisioned = (p->flags & SL2_SF_WIFI_PROVISIONED) != 0;
    s.wifi_setup_ap    = (p->flags & SL2_SF_SETUP_AP) != 0;
    s.sub_mode = action_substate_str(p->action);
    z->st = s; z->synced = true;
    if (is_active) {
        /* This applied event's receipt, not a newer callback's queued STATE. */
        atomic_store(&s_ota_state_rx_ms, received_ms);
        espnow_offline_alert_reset(&s_offline_alert);
        s_use_f = use_f;
        ui_set_state(&s);
        s_last_sent = s;
        ui_set_link_state(true, espnow_client_online(), z->bond.mac, z->rssi);
    }
    lvgl_port_unlock();
    screen_gate_sync();
    night_sync();
    if (epoch_moved && zi == s_active) resend_held_edits(zi);
}

static void room_catalog_req(int zi, uint16_t cursor, uint32_t known_rev) {
    struct sl2_room_catalog_req_pkt q = { SL2_PKT_ROOM_CATALOG_REQ,
                                          SL2_PROTO_VERSION, cursor, known_rev };
    if (cursor == 0) {
        s_catalog_stage_count = 0;
        s_catalog_stage_revision = 0;
        s_catalog_stage_zi = zi;
    }
    link_send(s_z[zi].bond.mac, &q, sizeof q, "ROOM_CATALOG_REQ");
}

static const char *room_source_name(int zi) {
    if (!catalog_held(zi)) return NULL;
    for (int i = 0; i < s_catalog_count; i++)
        if (s_catalog[i].id == s_z[zi].roomSourceId)
            return s_catalog[i].name;
    return NULL;
}

static void apply_caps(int zi, const struct sl2_caps_pkt *p) {
    zone_t *z = &s_z[zi];
    z->caps.modes        = p->modes;
    z->caps.features     = p->features;
    z->caps.fan_steps    = p->fan_steps;
    z->caps.fan_has_auto = (p->fan_flags & SL2_FAN_HAS_AUTO) != 0;
    z->caps.vane_v       = p->vane_v;
    z->caps.vane_h       = p->vane_h;
    z->caps.set_min_dc   = p->set_min_dc;
    z->caps.set_max_dc   = p->set_max_dc;
    z->caps.set_step_dc  = p->set_step_dc;
    z->caps.ftab_id      = p->ftab_id;
    z->caps.band_min_gap_dc = p->band_min_gap_dc;
    z->caps_seq = p->caps_seq;
    z->want_caps = false;
    if ((p->features & SL2_FEAT_ROOM_CATALOG) && zi == s_active &&
        !catalog_held(zi))
        room_catalog_req(zi, 0, 0);
    z->vaneWarned = false;
    char name[ESPNOW_BOND_NAME_LEN];
    memcpy(name, p->name, sizeof name);
    name[sizeof name - 1] = '\0';
    if (strncmp(z->bond.name, name, sizeof z->bond.name) != 0 ||
        z->bond.caps_seq != p->caps_seq) {
        memcpy(z->bond.name, name, sizeof z->bond.name);
        z->bond.name[sizeof z->bond.name - 1] = '\0';
        z->bond.caps_seq = p->caps_seq;
        espnow_bond_set_name(z->bond.mac, z->bond.name, z->bond.caps_seq);
    }
    if (zi == s_active && lvgl_port_lock(0)) {
        ui_set_caps(&z->caps);
        lvgl_port_unlock();
    }
}

static void apply_info_tlvs(int zi, const uint8_t *tlvs, size_t len) {
    zone_t *z = &s_z[zi];
    bool is_active = (zi == s_active);
    bool use_f = z->st.use_f;
    if (!lvgl_port_lock(0)) return;
    hvac_state_t s;
    if (is_active) ui_get_state(&s); else s = z->st;
    s.use_f = use_f;
    sl2_info_apply(&s, tlvs, len, use_f, z->caps.ftab_id);
    bool want_catalog = false;
    size_t roff = 0; uint8_t rt, rl; const uint8_t *rv;
    while (sl2_tlv_next(tlvs, len, &roff, &rt, &rl, &rv)) {
        if (rt == SL2_TLV_ROOM_SOURCE_V2 && rl >= sizeof(struct sl2_room_source_v2)) {
            struct sl2_room_source_v2 v; memcpy(&v, rv, sizeof v);
            z->roomSourceId = v.source_id; z->roomSourceStatus = v.status;
            if ((z->caps.features & SL2_FEAT_ROOM_CATALOG) && zi == s_active &&
                (!catalog_held(zi) || s_catalog_revision != v.revision))
                want_catalog = true;
        }
    }
    const char *rsname = room_source_name(zi);
    snprintf(s.room_src_name, sizeof s.room_src_name, "%s", rsname ? rsname : "");
    z->st = s;
    if (is_active) {
        ui_set_state(&s);
        ui_set_link_state(true, espnow_client_online(), z->bond.mac, z->rssi);
        ui_set_room_src(s.room_src, s.room_src_status);
        ui_set_room_source_id(z->roomSourceId, z->roomSourceStatus);
    }
    lvgl_port_unlock();
    if (want_catalog) room_catalog_req(zi, 0, 0);
}

static bool s_wake_due;
static espnow_events_t s_evq;
static volatile uint32_t s_rx_drop;

static void handle_evt(evt_t *it) {
    if (it->kind == EV_WAKE) { s_wake_due = true; return; }
    if (it->zi >= s_zn) return;
    if (it->kind == EV_CMD) {
        struct sl2_cmd_pkt c;
        memcpy(&c, it->data, sizeof c);
        cmd_tx(it->zi, &c);
        return;
    }
    if (it->kind == EV_TX) {
        if (it->data[0] == SL2_PKT_ROOM_SOURCE_SET &&
            !room_source_tx_current(it->data, it->len,
                                    s_z[it->zi].peerVer, s_z[it->zi].epoch)) {
            /* UI displays only confirmed selection; no held edit to retry. */
            return;
        }
        if ((it->data[0] == SL2_PKT_WIFI_SETUP && it->len == SL2_WIFI_SETUP_SESSION_MIN_LEN) ||
            (it->data[0] == SL2_PKT_WIFI_CANCEL && it->len == SL2_WIFI_CANCEL_MIN_LEN)) {
            uint32_t session, wire;
            memcpy(&session, it->data + offsetof(struct sl2_wifi_cancel_pkt, session), sizeof session);
            if (!wifi_setup_session_tx(&s_z[it->zi].wifiSession,
                    it->data[0] == SL2_PKT_WIFI_CANCEL, session, esp_random(), &wire)) return;
            memcpy(it->data + offsetof(struct sl2_wifi_cancel_pkt, session), &wire, sizeof wire);
        }
        if ((it->data[0] == SL2_PKT_WIFI_SETUP && it->len >= SL2_WIFI_SETUP_MIN_LEN) ||
            (it->data[0] == SL2_PKT_WIFI_CANCEL && it->len >= SL2_WIFI_CANCEL_MIN_LEN)) {
            uint16_t ep = s_z[it->zi].epoch;
            memcpy(it->data + offsetof(struct sl2_wifi_setup_pkt, epoch),
                   &ep, sizeof ep);
        }
        if (it->data[0] == SL2_PKT_WIFI_CANCEL) {
            s_wifi_cancel_tx_ms = now_ms();
            if (!s_wifi_cancel_rx_hold) {
                esp_err_t e = esp_wifi_force_wakeup_acquire();
                s_wifi_cancel_rx_hold = e == ESP_OK;
                if (e != ESP_OK)
                    ESP_LOGW(TAG, "Wi-Fi cancel receive hold failed: %s", esp_err_to_name(e));
            }
        }
        link_send(s_z[it->zi].bond.mac, it->data, it->len, "TX");
        return;
    }
    const uint8_t *data = it->data;
    int len = it->len;
    switch (data[0]) {
    case SL2_PKT_STATE: {
        struct sl2_state_pkt p;
        sl2_decode_pkt(&p, sizeof p, data, len);
        apply_state(it->zi, &p, it->received_ms);
        break;
    }
    case SL2_PKT_CAPS: {
        struct sl2_caps_pkt p;
        sl2_decode_pkt(&p, sizeof p, data, len);
        apply_caps(it->zi, &p);
        break;
    }
    case SL2_PKT_INFO:
        apply_info_tlvs(it->zi, data + SL2_INFO_HDR_LEN,
                        (size_t)len - SL2_INFO_HDR_LEN);
        break;
    case SL2_PKT_ROOM_CATALOG_RESP: {
        struct sl2_room_catalog_resp_hdr p;
        sl2_decode_pkt(&p, sizeof p, data, len);
        zone_t *z = &s_z[it->zi];
        size_t need = SL2_ROOM_CATALOG_RESP_HDR_LEN +
                      (size_t)p.count * sizeof(struct sl2_room_source_entry);
        if (p.count > SL2_ROOM_CATALOG_PAGE_MAX || need > (size_t)len ||
            s_catalog_stage_zi != it->zi ||
            (s_catalog_stage_count && p.revision != s_catalog_stage_revision)) {
            s_catalog_stage_count = 0;
            s_catalog_stage_revision = 0;
            break;
        }
        if (!s_catalog_stage_count) s_catalog_stage_revision = p.revision;
        uint8_t room = (uint8_t)(SL2_ROOM_CATALOG_MAX - s_catalog_stage_count);
        uint8_t take = p.count < room ? p.count : room;
        memcpy(s_catalog_stage + s_catalog_stage_count,
               data + SL2_ROOM_CATALOG_RESP_HDR_LEN,
               take * sizeof s_catalog_stage[0]);
        s_catalog_stage_count += take;
        if (p.next_cursor != SL2_ROOM_CATALOG_DONE) {
            room_catalog_req(it->zi, p.next_cursor, p.revision);
        } else {
            memcpy(s_catalog, s_catalog_stage,
                   s_catalog_stage_count * sizeof s_catalog[0]);
            s_catalog_count = s_catalog_stage_count;
            s_catalog_revision = s_catalog_stage_revision;
            s_catalog_zi = it->zi;
            s_catalog_stage_count = 0;
            s_catalog_stage_zi = -1;
            if (it->zi == s_active && lvgl_port_lock(0)) {
                ui_set_room_sources(s_catalog, s_catalog_count,
                                    z->roomSourceId, z->roomSourceStatus);
                ui_set_room_src_name(room_source_name(it->zi));
                lvgl_port_unlock();
            }
        }
        break;
    }
    case SL2_PKT_WIFI_CANCEL_ACK: {
        struct sl2_wifi_cancel_ack_pkt a;
        sl2_decode_pkt(&a, sizeof a, data, len);
        uint32_t session;
        if (it->zi == s_active &&
            wifi_setup_session_ack(&s_z[it->zi].wifiSession, a.session, &session) &&
            lvgl_port_lock(0)) {
            ui_wifi_cancel_result(session, a.status);
            lvgl_port_unlock();
        }
        break;
    }
    case SL2_PKT_ROOM_SOURCE_ACK: {
        struct sl2_room_source_ack_pkt a;
        sl2_decode_pkt(&a, sizeof a, data, len);
        zone_t *z = &s_z[it->zi];
        z->roomSourceId = a.source_id; z->roomSourceStatus = a.status;
        if (a.result == SL2_ROOM_SET_STALE_CATALOG && s_catalog_zi == it->zi)
            s_catalog_revision = 0;
        break;
    }
    default:
        break;
    }
}

static void evt_post(uint8_t kind, int zi, const void *buf, size_t len,
                     const char *what) {
    if (zi < 0 || zi >= s_zn || len > ESP_NOW_MAX_DATA_LEN) return;
    evt_t it;
    it.kind = kind;
    it.zi   = (uint8_t)zi;
    it.len  = (uint8_t)len;
    it.received_ms = 0;
    memcpy(it.data, buf, len);
    if (!espnow_events_post(&s_evq, &it))
        ESP_LOGW(TAG, "event queue full — %s dropped", what);
}
static void cmd_post(int zi, const struct sl2_cmd_pkt *c) {
    evt_post(EV_CMD, zi, c, sizeof *c, "CMD");
}

static void evt_pump(uint32_t wait_ms) {
    if (!s_evq.handle) return;
    uint32_t deadline = now_ms() + wait_ms;
    for (;;) {
        int32_t left = (int32_t)(deadline - now_ms());
        TickType_t t = left > 0 ? pdMS_TO_TICKS((uint32_t)left) : 0;
        evt_t it;
        if (!espnow_events_receive(&s_evq, &it, t)) {
            if (t == 0) return;
            continue;
        }
        handle_evt(&it);
        if (s_wake_due) return;
    }
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < 2) return;
    if (data[0] != SL2_PKT_PAIR_RESP &&
        memcmp(info->des_addr, s_own_mac, 6) != 0) return;
    const uint8_t ver = data[1];
    if (data[0] != SL2_PKT_PAIR_RESP && ver < SL2_PROTO_MIN_COMPAT) return;
    if (s_pairing && data[0] == SL2_PKT_PAIR_RESP &&
        len >= SL2_PAIR_MIN_LEN) {
        if (!s_have_resp) {
            sl2_decode_pkt(&s_resp, sizeof s_resp, data, len);
            s_have_resp = true;
        }
        return;
    }
    if (data[0] == SL2_PKT_PAIR_ACK) {
        /* Compare a precomputed key-bound proof. STATE may already be queued
         * or in flight under the previous LMK when a re-pair installs a key. */
        uint32_t received_ms = ms_stamp(now_ms());
        portENTER_CRITICAL(&s_pair_mux);
        if (s_pair_confirm && memcmp(info->src_addr, s_pair_mac, 6) == 0 &&
            sl2_pair_auth_matches(data, len, &s_pair_expected_ack))
            s_pair_ack_ms = received_ms;
        portEXIT_CRITICAL(&s_pair_mux);
        return;
    }

    int zi = zone_by_mac(info->src_addr);
    if (zi < 0) return;
    zone_t *z = &s_z[zi];
    if (info->rx_ctrl) z->rssi = info->rx_ctrl->rssi;
    z->peerVer = ver;
    switch (data[0]) {
    case SL2_PKT_WIFI_RESP:
        if (s_wifi_req_pending && len >= SL2_WIFI_RESP_MIN_LEN &&
            zi == s_active && !s_wifi_resp_have) {
            sl2_decode_pkt((void *)&s_wifi_resp, sizeof s_wifi_resp, data, len);
            s_wifi_resp_have = true;
        }
        return;
    case SL2_PKT_STATE: if (len < SL2_STATE_MIN_LEN) return; break;
    case SL2_PKT_CAPS:  if (len < SL2_CAPS_MIN_LEN)  return; break;
    case SL2_PKT_INFO:  if (len < SL2_INFO_HDR_LEN)  return; break;
    case SL2_PKT_ROOM_CATALOG_RESP:
        if (len < SL2_ROOM_CATALOG_RESP_HDR_LEN) return;
        break;
    case SL2_PKT_WIFI_CANCEL_ACK:
        if (len < SL2_WIFI_CANCEL_ACK_MIN_LEN) return;
        break;
    case SL2_PKT_ROOM_SOURCE_ACK:
        if (len < SL2_ROOM_SOURCE_ACK_MIN_LEN) return;
        break;
    default: return;
    }
    if (s_pairing || s_ota_hold) return;
    if (len > (int)sizeof ((evt_t *)0)->data) return;
    const uint32_t received = ms_stamp(now_ms());
    evt_t it;
    it.kind = EV_RX;
    it.zi   = (uint8_t)zi;
    it.len  = (uint8_t)len;
    it.received_ms = received;
    memcpy(it.data, data, (size_t)len);
    if (!espnow_events_post(&s_evq, &it)) { s_rx_drop++; return; }
    z->lastRxMs = received;
    if (espnow_state_reply(data, len)) {
        z->lastStateCh = s_radio_ch;
        z->lastStateRxMs = received;
    } else if (data[0] == SL2_PKT_CAPS) {
        z->lastCapsCh = s_radio_ch;
        z->lastCapsRxMs = received;
    }
}

static bool probe_channel(const uint8_t mac[6], uint8_t ch) {
    if (!radio_set_channel(ch)) return false;
    struct sl2_probe_pkt pr = { SL2_PKT_PROBE, SL2_PROTO_VERSION, SL2_WANT_STATE, {0} };
    return link_send(mac, &pr, sizeof pr, "PROBE");
}

static bool     s_sweeping;
static int      s_sweep_step;
static uint32_t s_sweep_rest_ms;
static volatile uint8_t s_sweep_fails;

static void sweep_backoff_reset(void) { s_sweep_fails = 0; }
#define SWEEP_STEP_GAP_MS  10

static void sweep_begin(void) {
    s_sweeping = true;
    s_sweep_step = 0;
    s_sweep_rest_ms = 0;
}
static bool sweep_hunting(void) { return s_sweeping && !s_sweep_rest_ms; }

static void sweep_end(void) {
    if (sweep_hunting()) radio_go_home();
    s_sweeping = false;
    sweep_backoff_reset();
}

static bool sweep_pass_due(uint32_t now) {
    if (!s_sweeping) return false;
    if (!s_sweep_rest_ms) return true;
    if ((uint32_t)(now - s_sweep_rest_ms) < espnow_relock_rest_ms(s_sweep_fails)) return false;
    s_sweep_rest_ms = 0;
    return true;
}

typedef enum { SWEEP_MISS, SWEEP_LOCKED, SWEEP_ROUND_END } sweep_step_t;

/* ACKs alone do not place the controller: the neighbouring channel returns
 * them too, with no application traffic. Pull STATE and keep the receiver
 * awake (power save would close it) until the reply is heard on this channel. */
static bool sweep_channel_answers(zone_t *z, uint8_t ch) {
    const bool held = esp_wifi_force_wakeup_acquire() == ESP_OK;
    const uint32_t tuned = ms_stamp(now_ms());
    bool acked = probe_channel(z->bond.mac, ch);
    /* A reply received before retuning does not confirm this channel. */
    if (acked && !zone_acked(s_active)) {
        probe_channel(z->bond.mac, ch);
        acked = zone_acked(s_active);
    }
    bool heard = false;
    while (acked) {
        heard = espnow_reply_since(tuned, z->lastStateRxMs, z->lastStateCh, ch);
        if (heard || (uint32_t)(now_ms() - tuned) >= ESPNOW_CH_CONFIRM_MS) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (held) esp_wifi_force_wakeup_release();
    if (acked && !heard)
        ESP_LOGI(TAG, "zone %d ch %u ACKs but does not answer — not locking",
                 s_active, ch);
    return espnow_lock_ok(acked, heard);
}

static sweep_step_t sweep_probe_once(uint32_t now) {
    zone_t *z = &s_z[s_active];
    const uint8_t remembered = z->bond.last_channel;
    uint8_t ch = espnow_relock_channel(s_sweep_step, remembered);
    if (!ch) {
        s_sweep_step = 0;
        s_sweep_rest_ms = ms_stamp(now);
        if (s_sweep_fails < UINT8_MAX) s_sweep_fails++;
        radio_go_home();
        ESP_LOGI(TAG, "zone %d band walked, nothing home (round %u) — resting %lu ms",
                 s_active, (unsigned)s_sweep_fails,
                 (unsigned long)espnow_relock_rest_ms(s_sweep_fails));
        return SWEEP_ROUND_END;
    }
    s_sweep_step++;
    if (!sweep_channel_answers(z, ch)) return SWEEP_MISS;
    s_channel = ch;
    s_sweeping = false;
    ESP_LOGI(TAG, "zone %d locked ch %u (%s, step %d)", s_active, ch,
             ch == remembered ? "remembered" : "scan", s_sweep_step);
    return SWEEP_LOCKED;
}

static uint8_t want_info_for_screen(void) {
    screen_id_t sc = ui_current_screen();
    return (sc == SCREEN_SETTINGS || sc == SCREEN_ABOUT || sc == SCREEN_WIFI) ? 1 : 0;
}

static uint8_t probe_want(int zi, uint8_t wi) {
    uint8_t w = wi ? SL2_WANT_INFO : 0;
    if (s_z[zi].want_caps)
        w |= SL2_WANT_CAPS;
    if (espnow_want_state(now_ms(), s_z[zi].lastRxMs, s_z[zi].synced))
        w |= SL2_WANT_STATE;
    /* An unchanged controller may otherwise send only a 10 s heartbeat.
       Verification needs actual STATE contact throughout its 30 s window. */
    if (s_ps_applied_min || (zi == s_active && link_ota_soaking()))
        w |= SL2_WANT_STATE;
    return w;
}

static bool probe_tx(int zi, uint8_t want, uint32_t now, const char *tag) {
    struct sl2_probe_pkt pr = { SL2_PKT_PROBE, SL2_PROTO_VERSION, want, {0} };
    bool acked = link_send(s_z[zi].bond.mac, &pr, sizeof pr, tag);
    stamp_first_probe(zi, now);
    if (!acked && s_radio_ch &&
        (s_radio_ch == s_z[zi].bond.last_channel ||
         (!s_z[zi].bond.last_channel && s_radio_ch == s_channel)))
        zone_ch_miss(zi);
    return acked;
}

static bool retune_adopt(uint8_t ch, const char *why) {
    if (!radio_set_channel(ch)) {
        ESP_LOGE(TAG, "%s retune -> ch %u failed", why, ch);
        return false;
    }
    s_channel = ch;
    return true;
}

static void cmd_put_temp(struct sl2_cmd_pkt *c, uint8_t ftab) {
    c->mask  |= SL2_CM_TEMP;
    c->set_dc = ui_dd_to_dc(s_set_hold.val, s_set_hold_use_f, ftab);
}
static void cmd_put_band(struct sl2_cmd_pkt *c, uint8_t ftab) {
    c->mask       |= SL2_CM_TEMP_BAND;
    c->set_low_dc  = ui_dd_to_dc(s_band_hold.lo, s_band_hold_use_f, ftab);
    c->set_high_dc = ui_dd_to_dc(s_band_hold.hi, s_band_hold_use_f, ftab);
}

static bool edits_pending(void) {
    return s_set_hold.on || s_band_hold.on || s_fan_hold.on;
}

static bool pending_edit_tx(struct sl2_cmd_pkt *txc, uint32_t now, bool flush) {
    const uint8_t ftab = s_z[s_active].caps.ftab_id;
    memset(txc, 0, sizeof *txc);
    txc->type = SL2_PKT_CMD; txc->version = SL2_PROTO_VERSION;
    if (edit_hold_tx_due(&s_set_hold, s_tx_sent_temp, now, EDIT_DEBOUNCE_MS, flush)) {
        cmd_put_temp(txc, ftab);
        s_tx_sent_temp = s_set_hold.val;
    }
    if (edit_hold2_tx_due(&s_band_hold, s_tx_sent_lo, s_tx_sent_hi, now,
                          EDIT_DEBOUNCE_MS, flush)) {
        cmd_put_band(txc, ftab);
        s_tx_sent_lo = s_band_hold.lo; s_tx_sent_hi = s_band_hold.hi;
    }
    if (edit_hold_tx_due(&s_fan_hold, s_tx_sent_fan, now, FAN_DEBOUNCE_MS, flush)) {
        txc->mask |= SL2_CM_FAN;
        txc->fan   = (uint8_t)s_fan_hold.val;
        s_tx_sent_fan = s_fan_hold.val;
    }
    return txc->mask != 0;
}

static void resend_held_edits(int zi) {
    struct sl2_cmd_pkt c;
    memset(&c, 0, sizeof c);
    c.type = SL2_PKT_CMD; c.version = SL2_PROTO_VERSION;
    if (!lvgl_port_lock(0)) return;
    const uint8_t ftab = s_z[zi].caps.ftab_id;
    if (s_mode_hold.on) { c.mask |= SL2_CM_MODE;
                         c.mode = mode_to_sl2((hvac_mode_t)s_mode_hold.val); }
    if (s_fan_hold.on)  { c.mask |= SL2_CM_FAN;   c.fan = (uint8_t)s_fan_hold.val;
                         s_tx_sent_fan = s_fan_hold.val; }
    if (s_vv_hold.on)   { c.mask |= SL2_CM_VANEV;
                         c.vane_v = vvane_to_sl2((vvane_t)s_vv_hold.val); }
    if (s_hv_hold.on)   { c.mask |= SL2_CM_VANEH;
                         c.vane_h = hvane_to_sl2((hvane_t)s_hv_hold.val,
                                                 s_z[zi].caps.vane_h); }
    if (s_unit_hold.on) { c.mask |= SL2_CM_UNITS; c.use_f = s_unit_hold.val ? 1 : 0; }
    if (s_set_hold.on)  { cmd_put_temp(&c, ftab); s_tx_sent_temp = s_set_hold.val; }
    if (s_band_hold.on) { cmd_put_band(&c, ftab);
                         s_tx_sent_lo = s_band_hold.lo; s_tx_sent_hi = s_band_hold.hi; }
    lvgl_port_unlock();
    if (!c.mask) return;
    ESP_LOGW(TAG, "zone %d replay epoch moved (unit rebooted) — re-asserting "
                  "held edits (mask 0x%04X)", zi, c.mask);
    cmd_tx(zi, &c);
}

static void do_zone_switch(int ni) {
    struct sl2_cmd_pkt flushc; bool flush_due = false;
    if (lvgl_port_lock(0)) {
        flush_due = pending_edit_tx(&flushc, now_ms(), true);
        edit_hold_drop(&s_set_hold);  edit_hold_drop(&s_unit_hold);
        edit_hold_drop(&s_mode_hold); edit_hold_drop(&s_fan_hold);
        edit_hold_drop(&s_vv_hold);   edit_hold_drop(&s_hv_hold);
        edit_hold2_drop(&s_band_hold);
        lvgl_port_unlock();
    }
    if (flush_due) cmd_tx(s_active, &flushc);
    s_tx_sent_temp = -1000; s_tx_sent_lo = s_tx_sent_hi = -1000; s_tx_sent_fan = -1000;
    int oi = s_active;
    atomic_store(&s_ota_state_rx_ms, 0);
    s_active = ni;
    espnow_offline_alert_reset(&s_offline_alert);
    prefs_set_u8("activeZone", (uint8_t)ni);
    catalog_reset();
    if (lvgl_port_lock(0)) {
        ui_set_room_sources(NULL, 0, s_z[ni].roomSourceId, s_z[ni].roomSourceStatus);
        ui_set_room_src_name(NULL);
        lvgl_port_unlock();
    }
    zone_t *z = &s_z[ni];
    if (espnow_switch_warm(z->synced, zone_acked(ni), zone_rx_stale(ni))) {
        bool xch = zone_cross_channel(ni);
        if (xch && z->bond.last_channel)
            retune_adopt(z->bond.last_channel, "warm switch");
        ESP_LOGI(TAG, "zone switch %d -> %d warm%s (ack %lu ms ago)", oi, ni,
                 xch ? " xch-hop" : "", (unsigned long)(now_ms() - z->lastAckMs));
        if (lvgl_port_lock(0)) {
            ui_set_caps(&z->caps);
            ui_set_state(&z->st);
            s_last_sent = z->st; ctrl_from(&z->st); s_use_f = z->st.use_f;
            ui_set_link_state(true, true, z->bond.mac, z->rssi);
            push_zones_ui_locked();
            lvgl_port_unlock();
        }
        return;
    }
    ESP_LOGI(TAG, "zone switch %d -> %d resync (synced %d, zone ch %u, radio ch %u)",
             oi, ni, z->synced, z->bond.last_channel, s_channel);
    if (lvgl_port_lock(0)) {
        ui_set_caps(&z->caps);
        if (hvac_state_plausible(&z->st)) {
            hvac_state_t st = z->st; st.alert = ALERT_SYNCING;
            ui_set_state(&st);
            s_last_sent = z->st; ctrl_from(&z->st); s_use_f = z->st.use_f;
        } else {
            hvac_state_t st;
            memset(&st, 0, sizeof st);
            st.alert = ALERT_SYNCING;
            ui_set_state(&st);
        }
        ui_set_link_state(true, false, z->bond.mac, z->rssi);
        push_zones_ui_locked();
        lvgl_port_unlock();
    }
    z->synced = false;
    if (z->bond.last_channel && z->bond.last_channel != s_channel)
        retune_adopt(z->bond.last_channel, "zone switch");
    s_sync_start_ms = now_ms();
}

static void copy_cstr(char *dst, size_t dstsz, const char *src) {
    size_t n = strnlen(src, dstsz - 1);
    memcpy(dst, src, n);
    dst[n] = 0;
}

#define DIAL_INFO_PERIOD_MS 60000
#define DIAL_INFO_RETRY_MS   5000

static bool maybe_send_dial_info(int zi, uint32_t now) {
    zone_t *z = &s_z[zi];
    bool retry  = !z->infoTxOk && zone_online(zi);
    uint32_t iv = retry ? DIAL_INFO_RETRY_MS : DIAL_INFO_PERIOD_MS;
    bool due = (z->infoTxMs == 0) ||
               (now - z->infoTxMs >= iv) ||
               (z->infoCapsSeq != z->caps_seq);
    if (!due) return false;
    struct sl2_dial_info_pkt di;
    memset(&di, 0, sizeof di);
    di.type = SL2_PKT_DIAL_INFO;
    di.version = SL2_PROTO_VERSION;
    di.caps_seq = z->caps_seq;
    copy_cstr(di.model, sizeof di.model, SERIN_MODEL_NAME);
    copy_cstr(di.fw, sizeof di.fw, esp_app_get_description()->version);
    z->infoTxOk = link_send(z->bond.mac, (const uint8_t *)&di, sizeof di, "DIAL_INFO");
    z->infoTxMs = ms_stamp(now);
    if (z->infoTxOk) z->infoCapsSeq = z->caps_seq;
    return true;
}

#define SENS_KEEPALIVE_MS  20000
#define SENS_CHANGE_DC        5

static bool maybe_send_dial_sensor(int zi, uint32_t now) {
    zone_t *z = &s_z[zi];
    if (!z->synced || !room_epoch_ready(z->peerVer, z->epoch)) return false;
    const bool wantScr = (z->caps.features & SL2_FEAT_SCREEN) != 0;
    if (!(z->caps.features & SL2_FEAT_LINK_SENSOR) && !wantScr) return false;
    if (!wantScr && !sensor_sht40_present()) return false;

    int16_t cc = SL2_CC_NA; uint16_t rh_cc = SL2_HUM_CC_NA;
    const bool have = sensor_sht40_read(&cc, &rh_cc, NULL);

    const bool scrOn = !ui_disp_asleep();

    const bool changed = have && (abs(cc / 10 - z->sensTxDc) >= SENS_CHANGE_DC);
    const bool scrMove = wantScr && (!z->scrTxValid || z->scrTxOn != scrOn);
    const bool due     = z->sensTxMs == 0 || now - z->sensTxMs >= SENS_KEEPALIVE_MS;
    if (!changed && !scrMove && !due) return false;

    struct sl2_dial_sensor_pkt p = {
        .type     = SL2_PKT_DIAL_SENSOR,
        .version  = SL2_PROTO_VERSION,
        .flags    = (sensor_sht40_present() ? SL2_DSF_HAS_SENSOR : 0u)
                  | (wantScr ? (uint8_t)(SL2_DSF_SCREEN_VALID |
                                         (scrOn ? SL2_DSF_SCREEN_ON : 0u)) : 0u),
        .temp_cc  = have ? cc : SL2_CC_NA,
        .hum_cc   = have ? rh_cc : SL2_HUM_CC_NA,
        .want_src = SL2_ROOMSRC_NOEDIT,
        .epoch    = z->epoch,
    };
    if (!link_send(z->bond.mac, &p, sizeof p, "DIAL_SENSOR")) return false;
    z->sensTxMs = ms_stamp(now);
    if (have) z->sensTxDc = (int16_t)(cc / 10);
    if (wantScr) { z->scrTxValid = true; z->scrTxOn = scrOn; }
    return true;
}

#define LINK_TASK_STACK 4096
static StaticTask_t s_link_tcb;
static StackType_t  s_link_stack[LINK_TASK_STACK / sizeof(StackType_t)];

static void link_task(void *arg) {
    (void)arg;
    s_link_task_up = true;
    esp_task_wdt_add(NULL);
    sweep_begin();
    s_sync_start_ms = now_ms();
    uint32_t lastProbe = 0;
    uint32_t stackLow = UINT32_MAX;
    uint32_t lastVerWarn = 0;
    uint32_t lastDropWarn = 0;
    uint32_t dropSeen = 0;
    uint8_t  lastWantInfo = 0;
    int      bgNext = 0;
    int      hopNext = 0;
    uint32_t lastHopMs = 0;
    uint32_t badgeSig = 0xFFFFFFFF;
    bool     wasHeld = false;
    uint32_t heldOnline = 0;
    uint32_t lastRadioTry = 0;
    for (;;) {
        esp_task_wdt_reset();
        if (s_wifi_cancel_rx_hold &&
            (uint32_t)(now_ms() - s_wifi_cancel_tx_ms) >= WIFI_CANCEL_REPLY_MS) {
            esp_err_t e = esp_wifi_force_wakeup_release();
            s_wifi_cancel_rx_hold = false;
            if (e != ESP_OK)
                ESP_LOGW(TAG, "Wi-Fi cancel receive release failed: %s", esp_err_to_name(e));
        }
        if (s_pairing || s_ota_hold) {
            if (!wasHeld) {
                wasHeld = true;
                espnow_offline_alert_reset(&s_offline_alert);
                heldOnline = 0;
                for (int i = 0; i < s_zn; i++)
                    if (zone_acked(i)) heldOnline |= 1u << i;
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (!s_radio_up) {
            uint32_t t = now_ms();
            if (lastRadioTry == 0 || (uint32_t)(t - lastRadioTry) >= 5000) {
                lastRadioTry = t ? t : 1;
                if (ensure_radio()) {
                    ESP_LOGW(TAG, "radio recovered after boot failure");
                    sweep_begin();
                    s_sync_start_ms = now_ms();
                } else if (lvgl_port_lock(0)) {
                    alert_set(ALERT_NOLINK);
                    lvgl_port_unlock();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (wasHeld) {
            wasHeld = false;
            for (int i = 0; i < s_zn; i++) {
                if (s_z[i].synced) s_z[i].lastRxMs = ms_stamp(now_ms());
                if (heldOnline & (1u << i)) s_z[i].lastAckMs = ms_stamp(now_ms());
            }
            espnow_events_reset(&s_evq);
            s_wake_due = false;
            radio_go_home();
            sweep_backoff_reset();
            if (s_sweeping) sweep_begin();
        }
        if (!sweep_hunting()) evt_pump(0);
        int req = s_zone_req;
        if (req >= 0) {
            s_zone_req = -1;
            if (req < s_zn && req != s_active) {
                int prevActive = s_active;
                sweep_end();
                do_zone_switch(req);
                hopNext = prevActive;
                lastProbe = 0;
            }
        }
        uint32_t now = now_ms();
        offline_alert_poll();

        if (sweep_pass_due(now)) {
            badge_paint_if_changed(&badgeSig);
            uint32_t shot = now_ms();
            sweep_step_t r = sweep_probe_once(now);
            if (r == SWEEP_LOCKED) {
                s_sync_start_ms = now_ms();
                lastProbe = now_ms();
            } else if (r == SWEEP_ROUND_END) {
                espnow_offline_alert_start(&s_offline_alert, now_ms());
            }
            uint32_t spent = now_ms() - shot;
            if (spent < SWEEP_STEP_GAP_MS)
                vTaskDelay(pdMS_TO_TICKS(SWEEP_STEP_GAP_MS - spent));
            continue;
        }
        if (s_z[s_active].peerVer && s_z[s_active].peerVer != SL2_PROTO_VERSION &&
            (lastVerWarn == 0 || now - lastVerWarn >= 60000)) {
            ESP_LOGW(TAG, "unit on proto v%u, dial on v%u (floor v%u) — update the %s",
                     s_z[s_active].peerVer, SL2_PROTO_VERSION, SL2_PROTO_MIN_COMPAT,
                     s_z[s_active].peerVer < SL2_PROTO_VERSION ? "unit" : "dial");
            lastVerWarn = now;
        }
        for (int i = 0; i < s_zn; i++) {
            uint8_t ach = s_z[i].lastAckCh;
            /* Every channel visit holds RX awake for STATE. ACKs alone can
             * come from a neighbouring channel and must not move the bond. */
            bool replied = espnow_reply_since(now - ESPNOW_RX_STALE_MS, s_z[i].lastStateRxMs,
                                              s_z[i].lastStateCh, ach);
            if (espnow_heal_due(zone_acked(i), ach, s_z[i].bond.last_channel, replied)) {
                ESP_LOGI(TAG, "zone %d channel heal %u -> %u", i,
                         s_z[i].bond.last_channel, ach);
                s_z[i].bond.last_channel = ach;
                espnow_bond_set_channel(s_z[i].bond.mac, ach);
            }
        }

        uint8_t wi = want_info_for_screen();
        if (wi != lastWantInfo) {
            probe_tx(s_active, probe_want(s_active, wi), now, "PROBE");
            lastWantInfo = wi; lastProbe = now;
        }
        const bool wake = s_wake_due;
        if (wake) s_wake_due = false;
        if (wake || now - lastProbe >= 1000) {
            probe_tx(s_active, probe_want(s_active, wi), now, "PROBE");
            lastProbe = now;
            if (!maybe_send_dial_info(s_active, now)) maybe_send_dial_sensor(s_active, now);
            push_zones_ui();
            screen_gate_sync();
            night_sync();

            int16_t scc = 0; uint16_t srh_cc = SL2_HUM_CC_NA;
            const bool svalid = sensor_sht40_read(&scc, &srh_cc, NULL);
            const int16_t sdc = (int16_t)((scc >= 0) ? (scc + 5) / 10 : (scc - 5) / 10);
            const uint8_t srh = (srh_cc == SL2_HUM_CC_NA)
                              ? SL2_HUM_NA : (uint8_t)((srh_cc + 50) / 100);
            if (lvgl_port_lock(0)) {
                ui_set_link_sensor(sensor_sht40_present(), svalid, sdc, srh);
                lvgl_port_unlock();
            }
            uint32_t drops = s_rx_drop;
            if (drops != dropSeen && (lastDropWarn == 0 || now - lastDropWarn >= 10000)) {
                ESP_LOGW(TAG, "RX queue full — dropped %lu frame(s) (link_task starved)",
                         (unsigned long)(drops - dropSeen));
                dropSeen = drops; lastDropWarn = now;
            }
            uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
            if (hw < stackLow) {
                stackLow = hw;
                ESP_LOGI(TAG, "link_task stack low-water %lu free of %d",
                         (unsigned long)hw, LINK_TASK_STACK);
            }
        }

        bool zones_open = (ui_current_screen() == SCREEN_ZONES);
        for (int n = 0; n < s_zn; n++) {
            int i = (bgNext + n) % s_zn;
            if (i == s_active) continue;
            /* Cross-channel telemetry belongs inside the visit below. */
            if (zone_cross_channel(i)) continue;
            uint32_t iv = zones_open ? ZONES_OPEN_PROBE_MS : BG_PROBE_MS;
            if (now - s_z[i].lastProbeMs < iv) continue;
            probe_tx(i, probe_want(i, 0), now, "PROBE");
            s_z[i].lastProbeMs = now;
            if (!maybe_send_dial_info(i, now)) maybe_send_dial_sensor(i, now);
            bgNext = (i + 1) % s_zn;
            break;
        }

        if ((now - lastHopMs) >= ZONE_HOP_INTERVAL_MS) {
            for (int n = 0; n < s_zn; n++) {
                int i = (hopNext + n) % s_zn;
                if (i == s_active) continue;
                /* Keep every controller alive while the picker is closed,
                 * including when the display sleeps. Per-zone cadence keeps
                 * several off-channel controllers from starving each other. */
                if (!zones_open && now - s_z[i].lastHopProbeMs < BG_PROBE_MS) continue;
                bool scan = espnow_chscan_due(s_z[i].chMiss, zone_online(i));
                uint8_t hopCh = scan ? zone_scan_next(i, now) : 0;
                if (!hopCh) {
                    if (!zone_cross_channel(i)) continue;
                    hopCh = s_z[i].bond.last_channel;
                    scan = false;
                }
                hopNext = (i + 1) % s_zn;
                lastHopMs = now;
                s_z[i].lastHopProbeMs = now;
                if (esp_wifi_force_wakeup_acquire() != ESP_OK) {
                    ESP_LOGW(TAG, "hop-poll could not hold the receiver awake");
                    break;
                }
                if (!radio_set_channel(hopCh)) {
                    ESP_LOGE(TAG, "hop-poll retune -> ch %u failed", hopCh);
                    esp_wifi_force_wakeup_release();
                    break;
                }
                const uint32_t tuned = ms_stamp(now_ms());
                const uint8_t want = (uint8_t)(probe_want(i, 0) | SL2_WANT_STATE);
                if (probe_tx(i, want, now, scan ? "SCANPROBE" : "HOPPROBE")) {
                    /* STATE may arrive before CAPS. A cold zone needs both
                     * before its sensor support is known; keep RX awake for
                     * the requested replies, within the same total budget. */
                    while ((!espnow_reply_since(tuned, s_z[i].lastStateRxMs,
                                                 s_z[i].lastStateCh, hopCh) ||
                            ((want & SL2_WANT_CAPS) &&
                             !espnow_reply_since(tuned, s_z[i].lastCapsRxMs,
                                                  s_z[i].lastCapsCh, hopCh))) &&
                           (uint32_t)(now_ms() - tuned) < ESPNOW_CH_CONFIRM_MS)
                        vTaskDelay(pdMS_TO_TICKS(10));
                    /* This visit's ACK plus STATE proves the channel even
                     * when no telemetry is due to supply a second ACK.
                     * STATE outranks that second ACK, so it also renews
                     * the background ACK window. */
                    if (espnow_reply_since(tuned, s_z[i].lastStateRxMs,
                                            s_z[i].lastStateCh, hopCh)) {
                        s_z[i].chMiss = 0;
                        s_z[i].ackWin = espnow_ack_window(false);
                        s_z[i].lastAckMs = ms_stamp(now_ms());
                        s_z[i].lastAckCh = hopCh;
                        if (s_z[i].bond.last_channel != hopCh) {
                            ESP_LOGI(TAG, "zone %d channel confirmed %u -> %u", i,
                                     s_z[i].bond.last_channel, hopCh);
                            s_z[i].bond.last_channel = hopCh;
                            espnow_bond_set_channel(s_z[i].bond.mac, hopCh);
                        }
                    }
                    /* Keep telemetry on the visited channel. Leave event
                     * processing until home: that queue also carries user
                     * commands for the active zone. A queued epoch change
                     * clears sensTxMs, forcing a fresh report next visit. */
                    maybe_send_dial_info(i, now_ms());
                    maybe_send_dial_sensor(i, now_ms());
                }
                for (int r = 0; r < 3; r++) {
                    if (radio_set_channel(s_channel))
                        break;
                    ESP_LOGE(TAG, "hop-poll home restore -> ch %u failed", s_channel);
                }
                esp_wifi_force_wakeup_release();
                now = now_ms();
                break;
            }
        }

        badge_paint_if_changed(&badgeSig);

        struct sl2_cmd_pkt txc; bool tx_due = false;
        if (edits_pending() && lvgl_port_lock(0)) {
            tx_due = pending_edit_tx(&txc, now, false);
            lvgl_port_unlock();
        }
        if (tx_due) cmd_tx(s_active, &txc);
        bool stuck_sync = (!s_z[s_active].synced && (now - s_sync_start_ms) > SYNC_WAIT_MS);
        bool rx_stale   = zone_rx_stale(s_active);
        if (espnow_hunt_due(espnow_client_online(), stuck_sync, rx_stale) && !s_sweeping) {
            bool lost = s_z[s_active].synced || stuck_sync;
            ESP_LOGW(TAG, "zone %d %s (ack %lu ms ago) — sweeping", s_active,
                     rx_stale     ? "rx stale (radio ACKs, app silent)"
                     : stuck_sync ? "locked but no STATE" : "link lost",
                     s_z[s_active].lastAckMs
                         ? (unsigned long)(now_ms() - s_z[s_active].lastAckMs) : 0UL);
            if (rx_stale) s_z[s_active].synced = false;
            s_z[s_active].ackWin = ESPNOW_ACK_WIN_ACTIVE_MS;
            if (lost) espnow_offline_alert_start(&s_offline_alert, now_ms());
            if (lvgl_port_lock(0)) { ui_set_link_state(true, false, s_z[s_active].bond.mac, s_z[s_active].rssi);
                                     push_zones_ui_locked();
                                     lvgl_port_unlock(); }
            sweep_begin();
            s_sync_start_ms = now_ms();
            lastProbe = now_ms();
        }
        evt_pump(200);
    }
}

void espnow_client_on_local_edit(void) {
    sweep_backoff_reset();
    if (!edit_tx_allowed(s_zn, s_z[s_active].synced)) return;
    hvac_state_t s; ui_get_state(&s);
    bool use_f = s_use_f;
    uint32_t now = now_ms();

    /* A sent value suppresses duplicates only within its current hold. After
     * confirmation or expiry, another controller may have changed the value;
     * selecting the earlier value again is a new command. While a hold is on,
     * the UI is compared with the hold, not with the controller's value, so
     * turning back to the controller's value cancels an unsent overshoot or
     * queues a correction for one that already went out. */
    bool set_changed = s_set_hold.on ? s.set_dd != s_set_hold.val
                                     : s.set_dd != s_ctrl_set_dd;
    if (set_changed) {
        if (s_set_hold.on && s_tx_sent_temp == -1000 &&
            s.set_dd == s_ctrl_set_dd) {
            edit_hold_drop(&s_set_hold);
        } else {
            if (!s_set_hold.on) s_tx_sent_temp = -1000;
            edit_hold_note(&s_set_hold, s.set_dd, now);
            s_set_hold_use_f = use_f;
        }
    }
    bool band_changed = s_band_hold.on
        ? (s.set_lo_dd != s_band_hold.lo || s.set_hi_dd != s_band_hold.hi)
        : (s.set_lo_dd != s_ctrl_lo_dd || s.set_hi_dd != s_ctrl_hi_dd);
    if (band_changed) {
        if (s_band_hold.on && s_tx_sent_lo == -1000 && s_tx_sent_hi == -1000 &&
            s.set_lo_dd == s_ctrl_lo_dd && s.set_hi_dd == s_ctrl_hi_dd) {
            edit_hold2_drop(&s_band_hold);
        } else {
            if (!s_band_hold.on) s_tx_sent_lo = s_tx_sent_hi = -1000;
            edit_hold2_note(&s_band_hold, s.set_lo_dd, s.set_hi_dd, now);
            s_band_hold_use_f = use_f;
        }
    }

    struct sl2_cmd_pkt c; memset(&c, 0, sizeof(c));
    c.type = SL2_PKT_CMD; c.version = SL2_PROTO_VERSION;
    if (s.mode != s_last_sent.mode)   { c.mask |= SL2_CM_MODE;  c.mode = mode_to_sl2(s.mode);
                                       edit_hold_note(&s_mode_hold, (int)s.mode, now); }
    bool fan_changed = s_fan_hold.on ? s.fan_pct != s_fan_hold.val
                                     : s.fan_pct != s_ctrl_fan;
    if (fan_changed) {
        if (s_fan_hold.on && s_tx_sent_fan == -1000 &&
            s.fan_pct == s_ctrl_fan) {
            edit_hold_drop(&s_fan_hold);
        } else {
            if (!s_fan_hold.on) s_tx_sent_fan = -1000;
            edit_hold_note(&s_fan_hold, s.fan_pct, now);
        }
    }
    if (s.vvane != s_last_sent.vvane) { c.mask |= SL2_CM_VANEV; c.vane_v = vvane_to_sl2(s.vvane);
                                       edit_hold_note(&s_vv_hold, (int)s.vvane, now); }
    if (s.hvane != s_last_sent.hvane) { c.mask |= SL2_CM_VANEH;
                                       c.vane_h = hvane_to_sl2(s.hvane, s_z[s_active].caps.vane_h);
                                       edit_hold_note(&s_hv_hold, (int)s.hvane, now); }
    if (c.mask == 0) return;
    cmd_post(s_active, &c);
    s_last_sent = s;
}

void espnow_client_on_units_edit(bool use_f) {
    if (!edit_tx_allowed(s_zn, s_z[s_active].synced)) return;
    /* Holds, sent values, and the controller's values are display values.
     * Every later STATE is converted with the new unit, so move them too;
     * a flip straight back restores them exactly. */
    static struct { int zone; ui_dd_flip_t set, sent, lo, hi, sent_lo, sent_hi, cset, clo, chi; } m;
    if (m.zone != s_active) { memset(&m, 0, sizeof m); m.zone = s_active; }  /* zone switched */
    const uint8_t ftab = s_z[s_active].caps.ftab_id;
    if (s_set_hold.on) {
        s_set_hold.val = ui_dd_flip(&m.set,  s_set_hold.val, s_set_hold_use_f, use_f, ftab);
        s_tx_sent_temp = ui_dd_flip(&m.sent, s_tx_sent_temp, s_set_hold_use_f, use_f, ftab);
        s_set_hold_use_f = use_f;
    }
    if (s_band_hold.on) {
        s_band_hold.lo = ui_dd_flip(&m.lo,      s_band_hold.lo, s_band_hold_use_f, use_f, ftab);
        s_band_hold.hi = ui_dd_flip(&m.hi,      s_band_hold.hi, s_band_hold_use_f, use_f, ftab);
        s_tx_sent_lo   = ui_dd_flip(&m.sent_lo, s_tx_sent_lo,   s_band_hold_use_f, use_f, ftab);
        s_tx_sent_hi   = ui_dd_flip(&m.sent_hi, s_tx_sent_hi,   s_band_hold_use_f, use_f, ftab);
        s_band_hold_use_f = use_f;
    }
    s_ctrl_set_dd = ui_dd_flip(&m.cset, s_ctrl_set_dd, s_use_f, use_f, ftab);
    s_ctrl_lo_dd  = ui_dd_flip(&m.clo,  s_ctrl_lo_dd,  s_use_f, use_f, ftab);
    s_ctrl_hi_dd  = ui_dd_flip(&m.chi,  s_ctrl_hi_dd,  s_use_f, use_f, ftab);
    edit_hold_note(&s_unit_hold, use_f ? 1 : 0, now_ms());
    s_use_f = use_f;
    s_z[s_active].st.use_f = use_f;
    struct sl2_cmd_pkt c; memset(&c, 0, sizeof(c));
    c.type = SL2_PKT_CMD; c.version = SL2_PROTO_VERSION;
    c.mask = SL2_CM_UNITS; c.use_f = use_f ? 1 : 0;
    cmd_post(s_active, &c);
}

void espnow_client_on_room_source_id(uint64_t source_id) {
    if (s_zn <= 0) return;
    zone_t *z = &s_z[s_active];
    if (!z->synced || !room_epoch_ready(z->peerVer, z->epoch) ||
        !(z->caps.features & SL2_FEAT_ROOM_CATALOG) || !catalog_held(s_active))
        return;
    bool found = false;
    for (int i = 0; i < s_catalog_count; i++)
        if (s_catalog[i].id == source_id) { found = true; break; }
    if (!found) return;
    struct sl2_room_source_set_pkt p = {0};
    p.type = SL2_PKT_ROOM_SOURCE_SET;
    p.version = SL2_PROTO_VERSION;
    p.request_id = ++z->roomRequestId;
    p.revision = s_catalog_revision;
    p.source_id = source_id;
    p.epoch = z->epoch;
    evt_post(EV_TX, s_active, &p, sizeof p, "ROOM_SOURCE_SET");
}

void espnow_client_ota_pause(bool paused) {
    if (paused) atomic_store(&s_offline_alert_reset_due, true);
    s_ota_hold = paused;
    if (!paused) {
        s_wifi_resp_have = false;
        memset((void *)&s_wifi_resp, 0, sizeof(s_wifi_resp));
    }
    if (s_radio_up) radio_ps_apply();
}

bool espnow_client_radio_off(bool off) {
    if (!s_radio_up) return false;
    if (off) {
        espnow_client_ota_pause(true);
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_err_t e = esp_wifi_stop();
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "radio off: wifi stop failed: %s", esp_err_to_name(e));
            espnow_client_ota_pause(false);
            return false;
        }
        ESP_LOGI(TAG, "radio off (rig)");
        return true;
    }
    esp_err_t e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGE(TAG, "radio on: wifi start failed: %s", esp_err_to_name(e));
        return false;
    }
    espnow_client_ota_pause(false);
    ESP_LOGI(TAG, "radio on (rig)");
    return true;
}

void espnow_client_wifi_setup(void)
{
    if (!s_zn) return;
    struct sl2_wifi_setup_pkt p;
    memset(&p, 0, sizeof(p));
    p.type = SL2_PKT_WIFI_SETUP; p.version = SL2_PROTO_VERSION;
    evt_post(EV_TX, s_active, &p, sizeof p, "WIFI_SETUP");
}

void espnow_client_wifi_setup_session(uint32_t session)
{
    struct sl2_wifi_setup_session_pkt p = {
        .type = SL2_PKT_WIFI_SETUP, .version = SL2_PROTO_VERSION, .session = session
    };
    evt_post(EV_TX, s_active, &p, sizeof p, "WIFI_SETUP_SESSION");
}

void espnow_client_wifi_cancel(uint32_t session)
{
    struct sl2_wifi_cancel_pkt p = {
        .type = SL2_PKT_WIFI_CANCEL, .version = SL2_PROTO_VERSION, .session = session
    };
    evt_post(EV_TX, s_active, &p, sizeof p, "WIFI_CANCEL");
}

espnow_wifi_req_t espnow_client_request_wifi(char *ssid, size_t ssid_len,
                                             char *psk, size_t psk_len, uint32_t timeout_ms)
{
    if (!s_zn) return ESPNOW_WIFI_REQ_NO_REPLY;
    s_wifi_resp_have = false;
    struct sl2_wifi_req_pkt rq;
    memset(&rq, 0, sizeof(rq));
    rq.type = SL2_PKT_WIFI_REQ; rq.version = SL2_PROTO_VERSION;
    uint32_t start = now_ms();
    s_wifi_req_pending = true;
    while (!s_wifi_resp_have && (now_ms() - start) < timeout_ms) {
        link_send(s_z[s_active].bond.mac, &rq, sizeof rq, "WIFI_REQ");
        for (int i = 0; i < 25 && !s_wifi_resp_have; i++)
            vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_wifi_req_pending = false;
    espnow_wifi_req_t res = !s_wifi_resp_have ? ESPNOW_WIFI_REQ_NO_REPLY
                          : s_wifi_resp.ok    ? ESPNOW_WIFI_REQ_OK
                                              : ESPNOW_WIFI_REQ_NO_CREDS;
    if (res == ESPNOW_WIFI_REQ_OK) {
        snprintf(ssid, ssid_len, "%.*s", (int)sizeof s_wifi_resp.ssid, s_wifi_resp.ssid);
        snprintf(psk,  psk_len,  "%.*s", (int)sizeof s_wifi_resp.psk,  s_wifi_resp.psk);
    }
    memset((void *)&s_wifi_resp, 0, sizeof(s_wifi_resp));
    s_wifi_resp_have = false;
    return res;
}

static int hexval(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static bool parse_mac(const char *s, uint8_t out[6]) {
    if (!s) return false;
    for (int i = 0; i < 6; i++) {
        int hi = hexval(s[0]), lo = hexval(s[1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
        s += 2;
        if (i < 5) { if (*s != ':') return false; s++; }
    }
    return *s == '\0';
}
static bool parse_hex16(const char *s, uint8_t out[16]) {
    if (!s) return false;
    for (int i = 0; i < 16; i++) {
        int hi = hexval(s[0]);
        if (hi < 0) return false;
        int lo = hexval(s[1]);
        if (lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
        s += 2;
    }
    return *s == '\0';
}

static int cmd_mac(int argc, char **argv) {
    (void)argc; (void)argv;
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    printf("ESPNOW-MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
           m[0], m[1], m[2], m[3], m[4], m[5]);
    return 0;
}

static int cmd_pair(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: espnow-pair <AA:BB:CC:DD:EE:FF> <32-hex-lmk>\n");
        return 1;
    }
    uint8_t mac[6], lmk[16];
    if (!parse_mac(argv[1], mac))   { printf("ERR bad mac\n");  return 1; }
    if (!parse_hex16(argv[2], lmk)) { printf("ERR bad lmk\n");  return 1; }
    espnow_bond_rec_t r; memset(&r, 0, sizeof r);
    memcpy(r.mac, mac, 6); memcpy(r.lmk, lmk, 16);
    int rc = espnow_bond_add(&r);
    if (rc < 0) {
        printf("ERR bond %s\n", rc == -1 ? "table full" : "persist failed");
        return 1;
    }
    if (link_ota_refuse_restart()) {
        printf("ESPNOW-PAIR OK; %s\n", LINK_OTA_SOAK_MSG);
        return 0;
    }
    printf("ESPNOW-PAIR OK; restarting\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return 0;
}

static int cmd_zones(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("radio ch %u, active zone %d, %d bonded\n", s_channel, s_active, s_zn);
    for (int i = 0; i < s_zn; i++) {
        uint32_t now = now_ms();
        uint32_t a = s_z[i].lastAckMs, r = s_z[i].lastRxMs;
        printf("%c z%d %02X:%02X:%02X:%02X:%02X:%02X ch%-2u %-7s synced=%d "
               "ack=%lums rx=%lums rssi=%d caps=%u%s '%s'\n",
               i == s_active ? '*' : ' ', i,
               s_z[i].bond.mac[0], s_z[i].bond.mac[1], s_z[i].bond.mac[2],
               s_z[i].bond.mac[3], s_z[i].bond.mac[4], s_z[i].bond.mac[5],
               s_z[i].bond.last_channel, s_badge_name[zone_badge(i)], s_z[i].synced,
               a ? (unsigned long)(now - a) : 0UL,
               r ? (unsigned long)(now - r) : 0UL,
               s_z[i].rssi, s_z[i].caps_seq, s_z[i].want_caps ? "+" : "",
               s_z[i].bond.name);
    }
    return 0;
}

static int cmd_zone(int argc, char **argv) {
    if (argc != 2) { printf("usage: zone <idx>\n"); return 1; }
    int n = (int)strtol(argv[1], NULL, 10);
    if (n < 0 || n >= s_zn) { printf("ERR no such zone (0..%d)\n", s_zn - 1); return 1; }
    espnow_client_set_active(n);
    printf("OK zone switch requested -> %d\n", n);
    return 0;
}

static int cmd_cmd(int argc, char **argv) {
    if (!s_zn) { printf("ERR no bond\n"); return 1; }
    if (argc != 3) { printf("usage: cmd <mode|temp|fan> <value>\n"); return 1; }
    long v = strtol(argv[2], NULL, 10);
    struct sl2_cmd_pkt c;
    memset(&c, 0, sizeof c);
    c.type = SL2_PKT_CMD; c.version = SL2_PROTO_VERSION;
    if (strcmp(argv[1], "mode") == 0)      { c.mask = SL2_CM_MODE; c.mode = (uint8_t)v; }
    else if (strcmp(argv[1], "temp") == 0) { c.mask = SL2_CM_TEMP; c.set_dc = (int16_t)v; }
    else if (strcmp(argv[1], "fan") == 0)  { c.mask = SL2_CM_FAN;  c.fan = (uint8_t)v; }
    else { printf("ERR unknown field\n"); return 1; }
    cmd_post(s_active, &c);
    printf("CMD %s=%ld -> zone %d\n", argv[1], v, s_active);
    return 0;
}

static int cmd_pair_start(int argc, char **argv) {
    (void)argc; (void)argv;
    if (s_pair_busy) { printf("ERR already pairing\n"); return 1; }
    printf("OK pairing started (45s sweep)\n");
    espnow_client_begin_pairing();
    return 0;
}

static void console_task(void *arg)
{
    (void)arg;
    bool hosted = false;
    for (;;) {
        if (!usb_serial_jtag_is_connected()) {
            hosted = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!hosted) {
            hosted = true;
            linenoiseSetDumbMode(linenoiseProbe() != 0);
        }
        char *line = linenoise("dial> ");
        if (!line) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (*line) {
            linenoiseHistoryAdd(line);
            int ret = 0;
            esp_err_t err = esp_console_run(line, &ret);
            if (err == ESP_ERR_NOT_FOUND)      printf("Unrecognized command\n");
            else if (err == ESP_OK && ret != 0) printf("Command returned non-zero error code: 0x%x\n", ret);
            else if (err != ESP_OK && err != ESP_ERR_INVALID_ARG)
                printf("Internal error: %s\n", esp_err_to_name(err));
        }
        linenoiseFree(line);
    }
}

static void register_console(void) {
    static bool s_console_started = false;
    if (s_console_started) return;
    s_console_started = true;

    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    fcntl(fileno(stdout), F_SETFL, 0);
    fcntl(fileno(stdin),  F_SETFL, 0);
    usb_serial_jtag_driver_config_t usjc = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&usjc) != ESP_OK) {
        ESP_LOGW(TAG, "console: USB-serial-JTAG driver install failed");
        return;
    }
    esp_console_config_t cc = ESP_CONSOLE_CONFIG_DEFAULT();
    if (esp_console_init(&cc) != ESP_OK) {
        ESP_LOGW(TAG, "console init failed");
        return;
    }
    usb_serial_jtag_vfs_use_driver();
    esp_console_register_help_command();
    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(16);
    linenoiseSetMaxLineLen(cc.max_cmdline_length);
    linenoiseAllowEmpty(false);

#define CONSOLE_TASK_STACK 8192
    static StaticTask_t s_console_tcb;
    static StackType_t  s_console_stack[CONSOLE_TASK_STACK / sizeof(StackType_t)];

    esp_console_cmd_t mac_cmd = {
        .command = "espnow-mac",
        .help    = "Print STA MAC for ESP-NOW pairing",
        .func    = cmd_mac,
    };
    esp_console_cmd_register(&mac_cmd);

    esp_console_cmd_t pair_cmd = {
        .command = "espnow-pair",
        .help    = "Bench-bond to a unit: espnow-pair <mac> <lmk_hex> (unpinned)",
        .func    = cmd_pair,
    };
    esp_console_cmd_register(&pair_cmd);

    esp_console_cmd_t zones_cmd = {
        .command = "zones",
        .help    = "Dump the bonded-zone table (channel/badge/ack age)",
        .func    = cmd_zones,
    };
    esp_console_cmd_register(&zones_cmd);

    esp_console_cmd_t zone_cmd = {
        .command = "zone",
        .help    = "Switch the active zone: zone <idx>",
        .func    = cmd_zone,
    };
    esp_console_cmd_register(&zone_cmd);

    esp_console_cmd_t pair_start_cmd = {
        .command = "pair",
        .help    = "Start v2 signed pairing (same as the UI pair verb)",
        .func    = cmd_pair_start,
    };
    esp_console_cmd_register(&pair_start_cmd);

    esp_console_cmd_t cmd_cmd_reg = {
        .command = "cmd",
        .help    = "Send a CMD to the active zone: cmd <mode|temp|fan> <value>",
        .func    = cmd_cmd,
    };
    esp_console_cmd_register(&cmd_cmd_reg);

    esp_console_cmd_t ps_cmd = {
        .command = "radio-ps",
        .help    = "Radio power save (bench lever): radio-ps [off|min [interval_ms [window_ms]]]",
        .func    = cmd_radio_ps,
    };
    esp_console_cmd_register(&ps_cmd);
    bsp_register_console();
    wdt_crumb_register_console();
    link_ota_register_console();
    if (xTaskCreateStatic(console_task, "console", sizeof s_console_stack, NULL, 2,
                          s_console_stack, &s_console_tcb) == NULL)
        ESP_LOGW(TAG, "console task create failed");
}

static void install_bond_peers(void) {
    for (int i = 0; i < s_zn; i++) {
        if (esp_now_is_peer_exist(s_z[i].bond.mac)) continue;
        esp_now_peer_info_t pi; memset(&pi, 0, sizeof(pi));
        memcpy(pi.peer_addr, s_z[i].bond.mac, 6);
        pi.ifidx = WIFI_IF_STA; pi.channel = 0; pi.encrypt = true;
        memcpy(pi.lmk, s_z[i].bond.lmk, 16);
        esp_err_t e = esp_now_add_peer(&pi);
        if (e != ESP_OK)
            ESP_LOGE(TAG, "zone %d peer install failed: %s — that zone stays offline",
                     i, esp_err_to_name(e));
    }
    esp_now_peer_num_t pn;
    if (s_zn && esp_now_get_peer_num(&pn) == ESP_OK)
        ESP_LOGI(TAG, "peers: %d total, %d encrypted (%d bonds, ceiling %d)",
                 pn.total_num, pn.encrypt_num, s_zn,
                 CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM);
}

bool espnow_client_link_alive(void) {
    uint32_t received = atomic_load(&s_ota_state_rx_ms);
    return received != 0 &&
           (uint32_t)(now_ms() - received) < ESPNOW_ACK_WIN_ACTIVE_MS;
}

#define RADIO_PS_PREF     "radio_ps"
#define RADIO_PS_IV_PREF  "radio_ps_iv"
#define RADIO_PS_WIN_PREF "radio_ps_win"
#define RADIO_PS_IV_DEF   1000
#define RADIO_PS_WIN_DEF  250
#define RADIO_PS_IV_MIN   100
#define RADIO_PS_WIN_MIN  50

static bool     s_ps_min;
static bool     s_ps_rig;
static bool     s_ps_rig_val;
static uint16_t s_ps_iv  = RADIO_PS_IV_DEF;
static uint16_t s_ps_win = RADIO_PS_WIN_DEF;

static void radio_ps_load(void) {
    s_ps_min = prefs_get_u8(RADIO_PS_PREF, 1) != 0;
    int16_t iv  = prefs_get_i16(RADIO_PS_IV_PREF,  RADIO_PS_IV_DEF);
    int16_t win = prefs_get_i16(RADIO_PS_WIN_PREF, RADIO_PS_WIN_DEF);
    s_ps_iv  = (uint16_t)(iv  < RADIO_PS_IV_MIN  ? RADIO_PS_IV_MIN  : iv);
    s_ps_win = (uint16_t)(win < RADIO_PS_WIN_MIN ? RADIO_PS_WIN_MIN : win);
}

static bool radio_ps_wanted(void) { return s_ps_rig ? s_ps_rig_val : s_ps_min; }

static void radio_ps_apply(void) {
    const bool min = radio_ps_wanted() && !s_ota_hold;
    if (min) {
        esp_err_t ei = esp_wifi_connectionless_module_set_wake_interval(s_ps_iv);
        esp_err_t ew = esp_now_set_wake_window(s_ps_win);
        if (ei != ESP_OK || ew != ESP_OK) {
            ESP_LOGE(TAG, "radio ps: wake interval %s, window %s — staying awake",
                     esp_err_to_name(ei), esp_err_to_name(ew));
            esp_wifi_set_ps(WIFI_PS_NONE);
            s_ps_applied_min = false;
            return;
        }
    }
    esp_err_t e = esp_wifi_set_ps(min ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    if (e == ESP_OK && min != s_ps_applied_min) {
        s_ps_applied_min = min;
        ESP_LOGI(TAG, "radio ps: %s%s", min ? "min-modem" : "none",
                 s_ota_hold ? " (held: STA associated)" : "");
        if (min) ESP_LOGI(TAG, "radio ps: window %u ms every %u ms", s_ps_win, s_ps_iv);
    } else if (e != ESP_OK) {
        ESP_LOGW(TAG, "radio ps: set_ps failed: %s", esp_err_to_name(e));
    }
}

static void on_wake_interval(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;
    if (!s_ps_applied_min) return;
    evt_t it = { .kind = EV_WAKE, .zi = 0, .len = 0 };
    espnow_events_post(&s_evq, &it);
}

void espnow_client_radio_ps_override(bool enable, bool min) {
    s_ps_rig = enable; s_ps_rig_val = min;
    if (s_radio_up) radio_ps_apply();
}

static int cmd_radio_ps(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "min") == 0) {
            prefs_set_u8(RADIO_PS_PREF, 1);
            if (argc >= 3) prefs_set_i16(RADIO_PS_IV_PREF,  (int16_t)atoi(argv[2]));
            if (argc >= 4) prefs_set_i16(RADIO_PS_WIN_PREF, (int16_t)atoi(argv[3]));
        } else if (strcmp(argv[1], "off") == 0) {
            prefs_set_u8(RADIO_PS_PREF, 0);
        } else {
            printf("usage: radio-ps [off|min [interval_ms [window_ms]]]\n");
            return 1;
        }
        radio_ps_load();
        if (s_radio_up) radio_ps_apply();
    }
    printf("radio-ps: %s  interval %u ms  window %u ms  (driver: %s%s)\n",
           s_ps_min ? "min" : "off", s_ps_iv, s_ps_win,
           s_ps_applied_min ? "min-modem" : "none",
           s_ps_rig ? ", rig override" : "");
    return 0;
}

static bool ensure_radio(void) {
    if (s_radio_up) return true;
    radio_ps_load();
    static bool sta_netif_created;
    esp_err_t e;

    if ((e = esp_netif_init()) != ESP_OK) {
        ESP_LOGE(TAG, "netif init failed: %s", esp_err_to_name(e)); return false;
    }
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop failed: %s", esp_err_to_name(e)); return false;
    }
    wifi_init_config_t wc = WIFI_INIT_CONFIG_DEFAULT();
    if ((e = esp_wifi_init(&wc)) != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %s (largest internal block %u)",
                 esp_err_to_name(e),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (!sta_netif_created) {
        if (!esp_netif_create_default_wifi_sta()) {
            ESP_LOGE(TAG, "create_default_wifi_sta failed"); return false;
        }
        sta_netif_created = true;
    }
    if ((e = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
        ESP_LOGE(TAG, "wifi set_mode failed: %s", esp_err_to_name(e)); return false;
    }
    e = esp_wifi_start();
    if (e != ESP_OK && e != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGE(TAG, "wifi start failed: %s (largest internal block %u)",
                 esp_err_to_name(e),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }
    esp_read_mac(s_own_mac, ESP_MAC_WIFI_STA);
    if ((e = esp_now_init()) != ESP_OK) {
        ESP_LOGE(TAG, "esp_now init failed: %s", esp_err_to_name(e));
        return false;
    }
    bool evq_ready = espnow_events_init(&s_evq);
    if (!s_tx_mux)  s_tx_mux  = xSemaphoreCreateMutex();
    if (!s_tx_done) s_tx_done = xSemaphoreCreateBinary();
    if (!evq_ready || !s_tx_mux || !s_tx_done) {
        ESP_LOGE(TAG, "link alloc failed (evq/txmux/txdone) — retrying");
        return false;
    }
    esp_now_set_pmk((const uint8_t *)SL2_ESPNOW_PMK);
    radio_ps_apply();
    esp_now_register_send_cb(on_send);
    esp_now_register_recv_cb(on_recv);
    static bool wake_evt_registered;
    if (!wake_evt_registered) {
        wake_evt_registered = esp_event_handler_register(
            WIFI_EVENT, WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START,
            on_wake_interval, NULL) == ESP_OK;
    }
    s_radio_up = true;
    install_bond_peers();
    return true;
}

static const uint8_t PAIR_BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define PAIR_TIMEOUT_MS 45000
#define PAIR_TASK_STACK 12288
static StaticTask_t s_pair_tcb;
static StackType_t  s_pair_stack[PAIR_TASK_STACK / sizeof(StackType_t)];

static void pair_peer_restore(const uint8_t mac[6]) {
    int zi = zone_by_mac(mac);
    if (zi >= 0) {
        esp_now_peer_info_t rp; memset(&rp, 0, sizeof rp);
        memcpy(rp.peer_addr, mac, 6);
        rp.ifidx = WIFI_IF_STA; rp.channel = 0; rp.encrypt = true;
        memcpy(rp.lmk, s_z[zi].bond.lmk, 16);
        if (esp_now_mod_peer(&rp) != ESP_OK)
            ESP_LOGE(TAG, "pair: LMK restore for zone %d failed", zi);
    } else if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
}

static void pair_run(void) {
    ESP_LOGI(TAG, "pair: task up, starting sweep");
    struct sl2_pair_resp_pkt resp;
    bool got_resp = false;
    uint8_t installed_mac[6] = {0};
    bool    peer_installed = false;
    bool    mismatch_seen = false;
    memset(&resp, 0, sizeof resp);
    if (!ensure_radio()) {
        ESP_LOGE(TAG, "pair aborted: radio unavailable");
        goto fail;
    }
    uint8_t id_priv[64], id_pub[32];
    if (!link_crypto_identity(id_priv, id_pub)) {
        ESP_LOGE(TAG, "pair aborted: no dial identity (NVS/libsodium)");
        goto fail;
    }
    if (!esp_now_is_peer_exist(PAIR_BCAST)) {
        esp_now_peer_info_t bp; memset(&bp, 0, sizeof(bp));
        memcpy(bp.peer_addr, PAIR_BCAST, 6);
        bp.ifidx = WIFI_IF_STA; bp.channel = 0; bp.encrypt = false;
        esp_now_add_peer(&bp);
    }
    if (link_crypto_x25519_keypair(s_pair_priv, s_pair_pub) != 0) goto fail;

    uint8_t my_mac[6]; esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
    struct sl2_pair_req_pkt req; memset(&req, 0, sizeof(req));
    req.type = SL2_PKT_PAIR_REQ; req.version = SL2_PROTO_VERSION;
    memcpy(req.src_mac, my_mac, 6);
    memcpy(req.eph_pub, s_pair_pub, 32);
    memcpy(req.id_pub, id_pub, 32);
    {
        uint8_t tr[SL2_REQ_TRANSCRIPT_LEN];
        sl2_pair_req_transcript(&req, tr);
        if (link_crypto_sign(id_priv, tr, sizeof tr, req.sig) != 0) goto fail;
    }

    if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_SEARCHING, 0); lvgl_port_unlock(); }
    atomic_store(&s_offline_alert_reset_due, true);
    s_have_resp = false; s_pairing = true;

    {
    uint8_t sib[ESPNOW_MAX_BONDS];
    int ns = bond_channels(-1, sib);
    uint32_t start = now_ms();
    while (now_ms() - start < PAIR_TIMEOUT_MS && !s_pair_cancel && !mismatch_seen) {
        for (int ci = 0; !s_pair_cancel; ci++) {
            uint8_t ch = espnow_chscan_channel(ci, 0, 0, sib, ns);
            if (!ch) break;
            if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK)
                ESP_LOGW(TAG, "pair: retune -> ch %u failed", ch);
            uint8_t gch = 0; wifi_second_chan_t g2; esp_wifi_get_channel(&gch, &g2);
            if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_SEARCHING, gch); lvgl_port_unlock(); }
            for (int rep = 0; rep < 6 && !s_pair_cancel; rep++) {
                link_send(PAIR_BCAST, &req, sizeof req, "PAIR_REQ");
                for (int i = 0; i < 5 && !s_pair_cancel; i++) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    if (!s_have_resp) continue;
                    memcpy(&resp, (const void *)&s_resp, sizeof resp);
                    s_have_resp = false;
                    uint8_t rtr[SL2_RESP_TRANSCRIPT_LEN];
                    sl2_pair_resp_transcript(&resp, s_pair_pub, rtr);
                    if (resp.type != SL2_PKT_PAIR_RESP ||
                        resp.version < SL2_PAIR_AUTH_MIN_VER ||
                        link_crypto_verify(resp.id_pub, rtr, sizeof rtr,
                                           resp.sig) != 0) {
                        ESP_LOGW(TAG, "pair: RESP signature invalid — ignoring");
                        continue;
                    }
                    int known = zone_by_mac(resp.src_mac);
                    if (known >= 0) {
                        static const uint8_t zero32[32];
                        if (memcmp(s_z[known].bond.peer_id_pub, zero32, 32) != 0 &&
                            memcmp(s_z[known].bond.peer_id_pub, resp.id_pub, 32) != 0) {
                            ESP_LOGE(TAG, "pair: controller identity changed — REFUSING "
                                          "(forget the zone first if this is intentional)");
                            mismatch_seen = true;
                            continue;
                        }
                    }
                    got_resp = true;
                    goto have_valid_resp;
                }
            }
        }
    }
    }
have_valid_resp:
    if (s_pair_cancel) goto cancelled;
    if (!got_resp) goto fail;
    ESP_LOGW(TAG, "pair: verified RESP from controller");

    {
    uint8_t lmk[16];
    if (link_crypto_derive_lmk(s_pair_priv, resp.eph_pub,
                               s_pair_pub, resp.eph_pub, lmk) != 0) {
        goto fail;
    }
    uint8_t unit_mac[6];
    memcpy(unit_mac, resp.src_mac, 6);

    esp_now_peer_info_t pi; memset(&pi, 0, sizeof(pi));
    memcpy(pi.peer_addr, unit_mac, 6); pi.ifidx = WIFI_IF_STA; pi.channel = 0; pi.encrypt = true;
    memcpy(pi.lmk, lmk, 16);
    esp_err_t pe = esp_now_add_peer(&pi);
    if (pe == ESP_ERR_ESPNOW_EXIST) pe = esp_now_mod_peer(&pi);
    if (pe != ESP_OK) { ESP_LOGE(TAG, "pairing: peer install failed (0x%x)", pe); goto fail; }
    memcpy(installed_mac, unit_mac, 6);
    peer_installed = true;
    { uint8_t cch; wifi_second_chan_t c2; esp_wifi_get_channel(&cch, &c2);
      ESP_LOGW(TAG, "pair: lmk+peer ok, PAIR_CONFIRM -> %02X:%02X:%02X:%02X:%02X:%02X on ch=%d",
               unit_mac[0],unit_mac[1],unit_mac[2],unit_mac[3],unit_mac[4],unit_mac[5], cch); }

    struct sl2_pair_auth_pkt pr, ack;
    sl2_pair_auth_make(&pr, SL2_PKT_PAIR_CONFIRM, lmk, my_mac, unit_mac);
    sl2_pair_auth_make(&ack, SL2_PKT_PAIR_ACK, lmk, my_mac, unit_mac);
    portENTER_CRITICAL(&s_pair_mux);
    memcpy(s_pair_mac, unit_mac, 6);
    s_pair_expected_ack = ack;
    s_pair_ack_ms = 0;
    s_pair_confirm = true;
    portEXIT_CRITICAL(&s_pair_mux);
    {
        uint8_t caught = 0; wifi_second_chan_t cc; esp_wifi_get_channel(&caught, &cc);
        if (resp.channel >= 1 && resp.channel <= 13 && resp.channel != caught) {
            ESP_LOGW(TAG, "pair: RESP caught on ch %u but controller lives on ch %u — retuning",
                     caught, resp.channel);
            if (esp_wifi_set_channel(resp.channel, WIFI_SECOND_CHAN_NONE) != ESP_OK)
                ESP_LOGW(TAG, "pair: retune -> ch %u failed (hunting by ACK instead)",
                         resp.channel);
        }
    }
    int acked = 0, sent = 0;
    for (int i = 0; i < 20 && s_pair_ack_ms == 0 && !s_pair_cancel; i++) {
        sent++;
        if (link_send(unit_mac, &pr, sizeof pr, "PAIR_CONFIRM")) acked++;
        if (!s_pair_cancel) vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (acked == 0 && s_pair_ack_ms == 0 && !s_pair_cancel) {
        ESP_LOGW(TAG, "pair: no ACKs — hunting the controller's channel");
        for (uint8_t ch = 1; ch <= 13 && !s_pair_cancel; ch++) {
            if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK)
                continue;
            for (int i = 0; i < 4 && !s_pair_cancel; i++) {
                sent++;
                if (link_send(unit_mac, &pr, sizeof pr, "PAIR_CONFIRM")) { acked++; break; }
            }
            if (acked) { ESP_LOGW(TAG, "pair: controller ACKed on ch %u", ch); break; }
        }
    }
    for (int i = 0; i < 100 && s_pair_ack_ms == 0 && !s_pair_cancel; i++) {
        sent++;
        if (link_send(unit_mac, &pr, sizeof pr, "PAIR_CONFIRM")) acked++;
        if (!s_pair_cancel) vTaskDelay(pdMS_TO_TICKS(20));
    }
    /* A radio ACK means the controller may already have stored the new key.
     * Give its authenticated ACK a bounded chance to arrive before cancel. */
    if (acked > 0 && s_pair_cancel && s_pair_ack_ms == 0) {
        uint32_t cancel_start = now_ms();
        while (s_pair_ack_ms == 0 && (uint32_t)(now_ms() - cancel_start) < 250)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGW(TAG, "pair: key confirmations sent=%d acked=%d", sent, acked);
    /* Stop accepting ACKs first so none lands after the check below. An
     * authenticated ACK means the controller already stored the new key:
     * it beats a cancel, and the old key must not come back. */
    pair_confirm_stop();
    if (s_pair_ack_ms != 0) {
        uint8_t ch = 0; wifi_second_chan_t c2; esp_wifi_get_channel(&ch, &c2);
        espnow_bond_rec_t rec; memset(&rec, 0, sizeof rec);
        memcpy(rec.mac, unit_mac, 6);
        memcpy(rec.lmk, lmk, 16);
        memcpy(rec.peer_id_pub, resp.id_pub, 32);
        rec.last_channel = ch;
        int idx = espnow_bond_add(&rec);
        if (idx < 0) {
            ESP_LOGE(TAG, "pair: bond %s — NOT paired",
                     idx == -1 ? "table full" : "persist failed");
            /* Keep a known zone on the controller's key until reboot. */
            int known = zone_by_mac(unit_mac);
            if (known >= 0) memcpy(s_z[known].bond.lmk, lmk, 16);
            goto fail;
        }
        prefs_set_u8("activeZone", (uint8_t)idx);
        ESP_LOGI(TAG, "pair: stack high-water %u free of %u",
                 (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)PAIR_TASK_STACK);
        if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_OK, 0); lvgl_port_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(800));
        if (link_ota_refuse_restart()) {
            s_pairing = false;
            /* A known zone must keep the controller's confirmed key in RAM. */
            int known = zone_by_mac(unit_mac);
            if (known >= 0) memcpy(s_z[known].bond.lmk, lmk, 16);
            if (esp_now_is_peer_exist(PAIR_BCAST)) esp_now_del_peer(PAIR_BCAST);
            if (lvgl_port_lock(0)) {
                ui_set_pairing(PAIR_UI_SAVED, 0);
                lvgl_port_unlock();
            }
            return;
        }
        esp_restart();
    }
    if (s_pair_cancel) goto cancelled;
    ESP_LOGW(TAG, "pair: no authenticated PAIR_ACK within ~6s");
    }
fail:
    s_pairing = false;
    pair_confirm_stop();
    ESP_LOGI(TAG, "pair: stack high-water %u free of %u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)PAIR_TASK_STACK);
    if (peer_installed) pair_peer_restore(installed_mac);
    if (esp_now_is_peer_exist(PAIR_BCAST)) esp_now_del_peer(PAIR_BCAST);
    if (lvgl_port_lock(0)) {
        ui_set_pairing((mismatch_seen && !got_resp) ? PAIR_UI_MISMATCH
                                                    : PAIR_UI_FAIL, 0);
        lvgl_port_unlock();
    }
    return;
cancelled:
    ESP_LOGW(TAG, "pair: cancelled by user");
    s_pairing = false;
    pair_confirm_stop();
    if (peer_installed) pair_peer_restore(installed_mac);
    if (esp_now_is_peer_exist(PAIR_BCAST)) esp_now_del_peer(PAIR_BCAST);
    if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_PROMPT, 0); lvgl_port_unlock(); }
}

static void (*volatile s_job_fn)(void *);
static void *volatile s_job_arg;

static void pair_task(void *arg) {
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_pair_go, portMAX_DELAY);
        void (*fn)(void *) = s_job_fn;
        if (fn) {
            void *a = s_job_arg;
            s_job_fn = NULL;
            fn(a);
            ESP_LOGI(TAG, "worker: job done, stack high-water %u free of %u",
                     (unsigned)uxTaskGetStackHighWaterMark(NULL),
                     (unsigned)PAIR_TASK_STACK);
        } else {
            pair_run();
            s_pair_busy = false;
        }
    }
}

bool espnow_client_worker_submit(void (*fn)(void *), void *arg) {
    if (!s_pair_task || s_pair_busy || s_job_fn) return false;
    s_job_arg = arg;
    s_job_fn  = fn;
    xSemaphoreGive(s_pair_go);
    return true;
}

void espnow_client_begin_pairing(void) {
    if (s_pair_busy) return;
    zones_init();
    if (s_zn >= ESPNOW_MAX_BONDS) {
        if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_FULL, 0); lvgl_port_unlock(); }
        return;
    }
    if (!s_pair_task) {
        ESP_LOGE(TAG, "pair: no pair task (boot create failed) — cannot pair");
        if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_FAIL, 0); lvgl_port_unlock(); }
        return;
    }
    s_pair_cancel = false;
    ESP_LOGI(TAG, "pair: waking pair task (internal heap free %u, largest block %u)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    s_pair_busy = true;
    xSemaphoreGive(s_pair_go);
}

void espnow_client_cancel_pairing(void) {
    if (s_pair_busy) s_pair_cancel = true;
}

void espnow_client_forget(void) {
    if (link_ota_refuse_restart()) return;
    zones_init();
    espnow_bond_remove(s_active);
    prefs_set_u8("activeZone", 0);
    esp_restart();
}

void espnow_client_boot_ui(void) {
    zones_init();
    if (!s_zn) return;
    ui_set_link_state(true, false, s_z[s_active].bond.mac, 0);
    alert_set(ALERT_SYNCING);
    push_zones_ui_locked();
}

void espnow_client_start(void) {
    register_console();

    s_pair_go = xSemaphoreCreateBinary();
    if (s_pair_go)
        s_pair_task = xTaskCreateStatic(pair_task, "espnow_pair", sizeof s_pair_stack,
                                        NULL, 5, s_pair_stack, &s_pair_tcb);
    if (!s_pair_task)
        ESP_LOGE(TAG, "pair: boot task create failed — pairing unavailable");

    zones_init();
    if (!s_zn) {
        ESP_LOGW(TAG, "no bond — showing pairing prompt");
        if (lvgl_port_lock(0)) { ui_set_pairing(PAIR_UI_PROMPT, 0); lvgl_port_unlock(); }
        return;
    }
    if (!ensure_radio())
        ESP_LOGE(TAG, "radio bring-up failed at boot — link_task will retry");

    if (xTaskCreateStatic(link_task, "espnow", sizeof s_link_stack, NULL, 5,
                          s_link_stack, &s_link_tcb) == NULL) {
        ESP_LOGE(TAG, "link task spawn FAILED (largest internal block %u) — "
                      "no ESP-NOW until reboot",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        if (lvgl_port_lock(0)) { alert_set(ALERT_NOLINK); lvgl_port_unlock(); }
    }
}

void espnow_client_self_mac(uint8_t out[6]) {
    esp_read_mac(out, ESP_MAC_WIFI_STA);
}

uint8_t espnow_client_peer_version(void) { return s_z[s_active].peerVer; }

int espnow_client_last_rx_age_s(void) {
    if (!s_zn) return -1;
    uint32_t t = s_z[s_active].lastRxMs;
    if (t == 0) return -1;
    return (int)((now_ms() - t) / 1000);
}
