# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Run actual bonded receive, state application, and OTA health production code.

Only the FreeRTOS queue storage and UI lock are edges. The production queue,
receive callback, STATE dispatch branch, and state application retain each
frame's identity and receipt time through FIFO processing. Tests
do not replace the health function with a scripted Boolean. Crypto/decryption
at the ESP-NOW driver boundary is outside this host test.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "main/espnow_client.c").read_text()


def function(signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


zone = source[source.index("typedef struct {\n    espnow_bond_rec_t bond;"):
              source.index("static const ui_caps_t ZONE_CAPS_DEFAULT")]
publication = re.search(r"^static _Atomic uint32_t s_ota_state_rx_ms;.*$", source, re.M)
publication = publication[0] if publication else "static _Atomic uint32_t s_ota_state_rx_ms;"
state_dispatch = source[source.index("    case SL2_PKT_STATE: {", source.index("static void handle_evt(")):
                        source.index("    case SL2_PKT_CAPS: {", source.index("static void handle_evt("))]
prelude = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "sl2_proto.h"
#include "sl2_pair_auth.h"
#include "sl2_ui_map.h"
#include "espnow_liveness.h"
#include "espnow_bond.h"
#include "wifi_setup_session.h"
#include "edit_hold.h"
#include "ui_dd.h"
#include "ota_ver.h"
#include "espnow_events.h"
#define SET_HOLD_MS 5000
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
typedef struct { int rssi; } wifi_pkt_rx_ctrl_t;
typedef struct {
    const uint8_t *src_addr, *des_addr;
    const wifi_pkt_rx_ctrl_t *rx_ctrl;
} esp_now_recv_info_t;
'''
edges = r'''
static zone_t s_z[ESPNOW_MAX_BONDS];
static int s_zn, s_active;
static uint8_t s_channel;
static uint32_t s_sync_start_ms;
static int s_tx_sent_temp, s_tx_sent_lo, s_tx_sent_hi, s_tx_sent_fan;
static bool s_radio_up, s_link_task_up, s_pairing, s_ota_hold, s_have_resp;
static bool s_ps_applied_min, pending_verification;
static bool s_pair_confirm, s_wifi_req_pending, s_wifi_resp_have;
static uint8_t s_pair_mac[6], s_own_mac[6], s_radio_ch;
static struct sl2_pair_resp_pkt s_resp;
static struct sl2_pair_auth_pkt s_pair_expected_ack;
static struct sl2_wifi_resp_pkt s_wifi_resp;
static uint32_t s_pair_ack_ms, s_rx_drop, clock_ms;
static int s_pair_mux;
static espnow_events_t s_evq;
struct fake_queue { evt_t items[8]; unsigned count; };
static struct fake_queue storage;
static bool queue_full, lock_available, process_in_send;
static uint32_t queue_delay_ms;
static hvac_state_t ui_state, s_last_sent;
static bool s_use_f;
static int s_ctrl_set_dd, s_ctrl_lo_dd, s_ctrl_hi_dd, s_ctrl_fan;
static edit_hold_t s_unit_hold, s_mode_hold, s_set_hold, s_fan_hold, s_vv_hold, s_hv_hold;
static edit_hold2_t s_band_hold;
static espnow_offline_alert_t s_offline_alert;
static uint32_t now_ms(void) { return clock_ms; }
static uint32_t ms_stamp(uint32_t now) { return now ? now : 1; }
bool link_ota_soaking(void) { return pending_verification; }
static void apply_queued(void);
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    assert(length == 8 && item_size == sizeof(evt_t));
    memset(&storage, 0, sizeof storage);
    return &storage;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *event, TickType_t wait) {
    assert(wait == 0);
    if (queue_full || q->count == 8) return pdFALSE;
    clock_ms += queue_delay_ms;
    memcpy(&q->items[q->count++], event, sizeof(evt_t));
    /* A higher-priority worker can consume the event before the sender returns. */
    if (process_in_send) apply_queued();
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *event, TickType_t wait) {
    (void)wait;
    if (!q->count) return pdFALSE;
    memcpy(event, &q->items[0], sizeof(evt_t));
    --q->count;
    memmove(q->items, q->items + 1, q->count * sizeof(evt_t));
    return pdTRUE;
}
BaseType_t xQueueReset(QueueHandle_t q) {
    q->count = 0;
    return pdTRUE;
}
static void sweep_backoff_reset(void) {}
static void vane_range_check(int zi, const struct sl2_state_pkt *p) { (void)zi; (void)p; }
static bool lvgl_port_lock(unsigned wait) { (void)wait; return lock_available; }
static void lvgl_port_unlock(void) {}
void ui_get_state(hvac_state_t *out) { *out = ui_state; }
void ui_set_state(const hvac_state_t *s) { ui_state = *s; }
void ui_set_link_state(bool bonded, bool online, const uint8_t mac[6], int rssi) {
    (void)bonded; (void)online; (void)mac; (void)rssi;
}
static bool espnow_client_online(void) { return true; } /* UI decoration only. */
static void screen_gate_sync(void) {}
static void night_sync(void) {}
static void resend_held_edits(int zi) { (void)zi; }
static const char *TAG = "test";
static void log_sink(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGI(tag, ...) log_sink(tag, __VA_ARGS__)
static bool pending_edit_tx(struct sl2_cmd_pkt *p, uint32_t now, bool flush) {
    (void)p; (void)now; (void)flush; return false;
}
static void cmd_tx(int zi, const struct sl2_cmd_pkt *p) { (void)zi; (void)p; }
static void prefs_set_u8(const char *key, uint8_t val) { (void)key; (void)val; }
static void catalog_reset(void) {}
static void ctrl_from(const hvac_state_t *s) { (void)s; }
static void push_zones_ui_locked(void) {}
static bool retune_adopt(uint8_t ch, const char *why) { (void)ch; (void)why; return true; }
void ui_set_room_sources(const struct sl2_room_source_entry *entries, int count,
                         uint64_t selected_id, uint8_t status) {
    (void)entries; (void)count; (void)selected_id; (void)status;
}
void ui_set_room_src_name(const char *name) { (void)name; }
void ui_set_caps(const ui_caps_t *caps) { (void)caps; }
static int cases, failures;
'''
tests = r'''
static void check(bool condition, const char *name) {
    ++cases;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s: health=%d at=%u\n", name, espnow_client_link_alive(), clock_ms);
}
static void prepare(void) {
    memset(s_z, 0, sizeof s_z);
    atomic_store(&s_ota_state_rx_ms, 0);
    s_zn = 2; s_active = 0; clock_ms = 10000;
    s_z[0].bond.mac[5] = 1; s_z[1].bond.mac[5] = 2;
    s_own_mac[5] = 9;
    s_radio_up = s_link_task_up = lock_available = true;
    s_pairing = s_ota_hold = queue_full = process_in_send = false;
    queue_delay_ms = 0;
    s_evq = (espnow_events_t){0};
    assert(espnow_events_init(&s_evq));
    s_ps_applied_min = false; pending_verification = true;
    ui_state = (hvac_state_t){0};
}
static void receive(int peer, struct sl2_state_pkt packet, int len) {
    const uint8_t stranger[6] = {0, 0, 0, 0, 0, 3};
    esp_now_recv_info_t info = {
        .src_addr = peer < 0 ? stranger : s_z[peer].bond.mac,
        .des_addr = s_own_mac,
    };
    on_recv(&info, (const uint8_t *)&packet, len);
}
static void apply_queued(void) {
    evt_t queued;
    if (!espnow_events_receive(&s_evq, &queued, 0)) return;
    assert(queued.kind == EV_RX && queued.data[0] == SL2_PKT_STATE);
    apply_state_event(&queued);
}
int main(void) {
    struct sl2_state_pkt packet = {
        .type = SL2_PKT_STATE, .version = SL2_PROTO_VERSION, .epoch = 1,
        .mode = SL2_MODE_OFF, .set_dc = 210, .room_dc = 200,
        .set_low_dc = SL2_DC_NA, .set_high_dc = SL2_DC_NA,
    };
    prepare();
    check(!espnow_client_link_alive(), "radio and worker startup without controller is unhealthy");
    s_zn = 0;
    check(!espnow_client_link_alive(), "no bonds cannot establish controller health");
    prepare(); receive(-1, packet, sizeof packet); apply_queued();
    check(!espnow_client_link_alive(), "unbonded STATE cannot establish health");
    prepare(); receive(1, packet, sizeof packet); apply_queued();
    check(!espnow_client_link_alive(), "background controller cannot verify selected controller");
    prepare(); receive(0, packet, SL2_STATE_MIN_LEN - 1); apply_queued();
    check(!espnow_client_link_alive(), "truncated STATE cannot establish health");
    prepare(); packet.version = SL2_PROTO_MIN_COMPAT - 1;
    receive(0, packet, sizeof packet); apply_queued();
    check(!espnow_client_link_alive(), "incompatible STATE cannot establish health");
    packet.version = SL2_PROTO_VERSION;
    prepare(); queue_full = true; receive(0, packet, sizeof packet); apply_queued();
    check(!espnow_client_link_alive(), "dropped queue event cannot establish health");
    prepare(); receive(0, packet, sizeof packet);
    check(!espnow_client_link_alive(), "callback alone cannot prove worker applied a state");
    apply_queued();
    check(espnow_client_link_alive() && s_z[0].synced, "fresh bonded applied STATE establishes health");
    clock_ms += 3999;
    check(espnow_client_link_alive(), "state remains fresh within active contact window");
    ++clock_ms;
    check(!espnow_client_link_alive(), "state expires exactly at four seconds even with startup flags up");
    s_z[0].lastAckMs = s_z[0].lastRxMs = clock_ms;
    check(!espnow_client_link_alive(), "radio ACK and synthetic receive grace cannot renew state evidence");
    receive(0, packet, sizeof packet); apply_queued();
    check(espnow_client_link_alive(), "fresh state restores health after dropout");
    prepare(); packet.fault = 123; packet.flags &= ~SL2_SF_HVAC_LINK;
    receive(0, packet, sizeof packet); apply_queued();
    check(espnow_client_link_alive() && ui_state.alert == ALERT_FAULT && !ui_state.cn105_connected,
          "heat pump fault and disconnected CN105 do not fail controller health");
    prepare(); receive(0, packet, sizeof packet); clock_ms += 4000; apply_queued();
    check(!espnow_client_link_alive(), "applying an old queued frame does not refresh receipt time");
    prepare(); lock_available = false; receive(0, packet, sizeof packet); apply_queued();
    check(!espnow_client_link_alive(), "state not applied because UI lock failed cannot establish health");
    prepare(); clock_ms = UINT32_MAX - 2000; receive(0, packet, sizeof packet); apply_queued();
    clock_ms += 3999;
    check(espnow_client_link_alive(), "freshness survives 32 bit timer wrap");
    ++clock_ms;
    check(!espnow_client_link_alive(), "wrapped timer still expires state at four seconds");

    prepare(); receive(0, packet, sizeof packet); apply_queued();
    check(probe_want(0, 0) & SL2_WANT_STATE,
          "pending verification requests STATE even when synchronized and radio PS is off");
    receive(1, packet, sizeof packet); apply_queued();
    check(!(probe_want(1, 0) & SL2_WANT_STATE), "verification does not force background STATE requests");
    pending_verification = false;
    check(!(probe_want(0, 0) & SL2_WANT_STATE), "accepted image restores normal idle request cadence");

    prepare(); receive(0, packet, sizeof packet); apply_queued();
    ota_guard_t guard = {.healthy_since_ms = -1};
    ota_guard_act_t action = OTA_GUARD_WAIT;
    /* An unchanged idle controller normally sends only a 10 s heartbeat.
       Only an explicit STATE request makes it answer earlier in this fixture. */
    for (uint32_t elapsed = 0; elapsed <= 30000; elapsed += 1000) {
        clock_ms = 10000 + elapsed;
        if ((probe_want(0, 0) & SL2_WANT_STATE) || elapsed % 10000 == 0) {
            receive(0, packet, sizeof packet); apply_queued();
        }
        if (elapsed % 2000 == 0)
            action = ota_guard_decide(&guard, elapsed, espnow_client_link_alive(),
                                      true, 30000, 600000, 4000);
    }
    check(action == OTA_GUARD_VALID,
          "ordinary idle controller with radio PS off completes real observed health window");

    prepare(); receive(0, packet, sizeof packet); apply_queued();
    receive(1, packet, sizeof packet); apply_queued();
    s_z[1].lastAckMs = clock_ms; s_z[1].ackWin = ESPNOW_ACK_WIN_ACTIVE_MS;
    do_zone_switch(1);
    check(!espnow_client_link_alive(), "warm selection change discards cached controller evidence");
    receive(0, packet, sizeof packet); apply_queued();
    check(!espnow_client_link_alive(), "previous active controller cannot renew selection health");
    receive(1, packet, sizeof packet); apply_queued();
    check(espnow_client_link_alive(), "new selected controller verifies through a fresh applied state");
    do_zone_switch(0); /* Cold switch: no fresh delivery ACK for zone zero. */
    check(!espnow_client_link_alive(), "cold selection change also discards controller evidence");

    prepare(); packet.set_dc = 210;
    receive(0, packet, sizeof packet); /* A received at 10 s. */
    clock_ms = 15000; packet.set_dc = 220;
    receive(0, packet, sizeof packet); /* B received at 15 s, still unapplied. */
    apply_queued();
    check(ui_state.set_dd == 210 && storage.count == 1,
          "FIFO applies A while newer B remains queued");
    check(!espnow_client_link_alive(), "unapplied newer B cannot lend freshness to stale applied A");
    apply_queued();
    check(espnow_client_link_alive() && ui_state.set_dd == 220 && storage.count == 0,
          "applying fresh B publishes its own receipt evidence");
    clock_ms = 19000;
    check(!espnow_client_link_alive(), "B freshness expires from its own receipt time");

    prepare(); process_in_send = true;
    receive(0, packet, sizeof packet);
    check(espnow_client_link_alive() && storage.count == 0,
          "immediate dequeue before callback returns already has receipt timestamp");
    prepare(); process_in_send = true; queue_delay_ms = 5000;
    receive(0, packet, sizeof packet);
    check(!espnow_client_link_alive() && storage.count == 0,
          "scheduler delay during enqueue cannot refresh the event receipt time");
    printf("ota_link_health: %d/%d passed\n", cases - failures, cases);
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="serin-ota-link-health-") as build_dir:
    unit = Path(build_dir) / "health.c"
    binary = Path(build_dir) / "health"
    unit.write_text(prelude + zone + publication + "\n" + edges
                    + function("static int zone_by_mac(")
                    + function("static bool zone_cross_channel(")
                    + function("static bool zone_acked(")
                    + function("static bool zone_rx_stale(")
                    + function("static void apply_state(")
                    + function("static void on_recv(")
                    + function("static uint8_t probe_want(")
                    + function("static void do_zone_switch(")
                    + function("bool espnow_client_link_alive(")
                    + "static void apply_state_event(evt_t *it) {\n"
                    + "const uint8_t *data = it->data; int len = it->len;\n"
                    + "switch (data[0]) {\n" + state_dispatch + "default: assert(false); }\n}\n"
                    + tests)
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "test/espnow_events/stub"),
                    "-I" + str(root / "main"), str(unit), "-o", str(binary), "-lm"], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
