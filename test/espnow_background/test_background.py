# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Compile the real client's background polling block with radio/time stubs.

The client translation unit depends on board, UI, and FreeRTOS APIs. Extract
its unchanged production block and sensor-report function, rather than copy
those decisions into a second implementation. Keep the assertions about
on-air delivery, not source text: probes alone must not pass the sensor test.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'main/espnow_client.c').read_text()

def function(signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]

constants = source[source.index('#define BG_PROBE_MS'):source.index('static int zone_by_mac(')]
sensor_constants = source[source.index('#define SENS_KEEPALIVE_MS'):source.index('static bool maybe_send_dial_sensor(')]
block = source[source.index('        bool zones_open = (ui_current_screen()'):source.index('        badge_paint_if_changed(&badgeSig);', source.index('        bool zones_open = (ui_current_screen()'))]
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "espnow_liveness.h"
#include "espnow_chscan.h"
#define ESP_OK 0
#define SCREEN_ZONES 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(ms) (ms)
typedef struct {
    struct { uint8_t mac[6], last_channel; } bond;
    bool synced;
    uint8_t peerVer;
    uint16_t epoch;
    struct { uint16_t features; } caps;
    uint32_t lastProbeMs, lastHopProbeMs, sensTxMs, lastStateRxMs, lastCapsRxMs;
    uint8_t lastStateCh, lastCapsCh, chMiss;
    uint32_t pendAckMs, lastAckMs, ackWin;
    uint8_t pendAckCh, lastAckCh;
    bool want_caps;
    int16_t sensTxDc;
    bool scrTxValid, scrTxOn;
} zone_t;
static zone_t s_z[4];
static uint32_t clock_ms, lastHopMs;
static int s_active, s_zn, bgNext, hopNext, screen, radio_ch;
static uint8_t s_channel;
static int holds, sensor_count[4], probe_count[4];
static uint8_t controller_channel[4], saved_channel[4];
static uint32_t reply_delay_ms, reply_started_ms;
static uint32_t caps_delay_ms, caps_started_ms;
static int caps_zone, queued_caps;
static int reply_zone, scan_zone, scan_step, fail_tune_channel, fail_tune_count;
static bool fail_wake, queued_command;
static bool lossy[4], silent[4];
static unsigned probe_attempts[4];
static unsigned command_count;
static void espnow_bond_set_channel(const uint8_t mac[6], uint8_t ch) { saved_channel[mac[5]] = ch; }
static uint32_t last_probe[4], last_sensor[4], max_probe_gap[4], max_sensor_gap[4];
static uint32_t now_ms(void) { return clock_ms; }
static uint32_t ms_stamp(uint32_t t) { return t ? t : 1; }
static int ui_current_screen(void) { return screen; }
static bool ui_disp_asleep(void) { return screen != SCREEN_ZONES; }
static bool sensor_sht40_present(void) { return true; }
static bool sensor_sht40_read(int16_t *cc, uint16_t *rh, void *unused) {
    (void)unused; *cc = 2250; *rh = 4850; return true;
}
static bool room_epoch_ready(uint8_t version, uint16_t epoch) { return version && epoch; }
static bool zone_cross_channel(int i) { return s_z[i].bond.last_channel != s_channel; }
static void note_delivery(int i, uint8_t ch);
static bool link_zone_online(int i);
/* The scan test pins its zone offline so only the miss count can stop a scan. */
static bool zone_online(int i) { return i != scan_zone && link_zone_online(i); }
static uint8_t zone_scan_next(int i, uint32_t now) {
    (void)i; (void)now; return scan_step++ == 0 ? 6 : 11;
}
static bool radio_set_channel(uint8_t ch) {
    if (ch == fail_tune_channel && fail_tune_count > 0) { --fail_tune_count; return false; }
    radio_ch = ch; return true;
}
static void vTaskDelay(uint32_t ms) {
    clock_ms += ms;
    if (reply_zone >= 0 && reply_delay_ms != UINT32_MAX && holds &&
        radio_ch == controller_channel[reply_zone] && clock_ms - reply_started_ms >= reply_delay_ms) {
        s_z[reply_zone].lastStateRxMs = ms_stamp(clock_ms);
        s_z[reply_zone].lastStateCh = radio_ch;
        reply_zone = -1;
    }
    if (caps_zone >= 0 && caps_delay_ms != UINT32_MAX && holds &&
        radio_ch == controller_channel[caps_zone] && clock_ms - caps_started_ms >= caps_delay_ms) {
        s_z[caps_zone].lastCapsRxMs = ms_stamp(clock_ms);
        s_z[caps_zone].lastCapsCh = radio_ch;
        queued_caps = caps_zone;
        caps_zone = -1;
    }
}
static int esp_wifi_force_wakeup_acquire(void) {
    if (fail_wake) return -1;
    ++holds; return ESP_OK;
}
static int esp_wifi_force_wakeup_release(void) { --holds; return ESP_OK; }
static void evt_pump(unsigned timeout) {
    (void)timeout; assert(radio_ch == s_channel);
    if (queued_command) { ++command_count; queued_command = false; }
    if (queued_caps >= 0) {
        s_z[queued_caps].caps.features = SL2_FEAT_LINK_SENSOR;
        s_z[queued_caps].want_caps = false;
        queued_caps = -1;
    }
}
static uint8_t probe_want(int i, uint8_t wi) {
    (void)wi; return s_z[i].want_caps ? SL2_WANT_CAPS : 0;
}
static bool probe_tx(int i, uint8_t want, uint32_t now, const char *tag) {
    (void)now; (void)tag;
    if (radio_ch != controller_channel[i] || silent[i]) return false;
    /* A marginal link: every second probe goes unacknowledged, so the
     * dial skips that visit's reply wait. */
    if (lossy[i] && (probe_attempts[i]++ & 1)) return false;
    note_delivery(i, radio_ch);
    if (probe_count[i]) {
        uint32_t gap = clock_ms - last_probe[i];
        if (gap > max_probe_gap[i]) max_probe_gap[i] = gap;
    }
    ++probe_count[i]; last_probe[i] = clock_ms;
    if (want & SL2_WANT_CAPS) { caps_zone = i; caps_started_ms = clock_ms; }
    if (holds) {
        if (reply_delay_ms == 0) {
            s_z[i].lastStateRxMs = ms_stamp(clock_ms); s_z[i].lastStateCh = radio_ch;
        } else { reply_zone = i; reply_started_ms = clock_ms; }
    }
    return true;
}
static bool maybe_send_dial_info(int i, uint32_t now) { (void)i; (void)now; return false; }
static bool link_send(const uint8_t mac[6], const void *data, size_t len, const char *tag) {
    (void)tag;
    int i = mac[5];
    if (radio_ch != controller_channel[i] || silent[i]) return false;
    note_delivery(i, radio_ch);
    const struct sl2_dial_sensor_pkt *packet = data;
    assert(len == sizeof *packet && packet->type == SL2_PKT_DIAL_SENSOR);
    assert(packet->temp_cc == 2250 && packet->hum_cc == 4850);
    assert(packet->want_src == SL2_ROOMSRC_NOEDIT && packet->epoch == s_z[i].epoch);
    if (sensor_count[i]) {
        uint32_t gap = clock_ms - last_sensor[i];
        if (gap > max_sensor_gap[i]) max_sensor_gap[i] = gap;
    }
    ++sensor_count[i]; last_sensor[i] = clock_ms;
    return true;
}
'''
tests = r'''
static void reset(int selected_screen, uint32_t start) {
    memset(s_z, 0, sizeof s_z);
    memset(sensor_count, 0, sizeof sensor_count);
    memset(probe_count, 0, sizeof probe_count);
    memset(max_sensor_gap, 0, sizeof max_sensor_gap);
    memset(max_probe_gap, 0, sizeof max_probe_gap);
    clock_ms = start; screen = selected_screen;
    s_zn = 4; s_active = 0; s_channel = radio_ch = 1;
    holds = bgNext = hopNext = 0; lastHopMs = 0;
    reply_delay_ms = 0; reply_zone = scan_zone = fail_tune_channel = -1;
    caps_delay_ms = 60; caps_zone = queued_caps = -1;
    scan_step = fail_tune_count = 0; fail_wake = queued_command = false; command_count = 0;
    memset(lossy, 0, sizeof lossy); memset(silent, 0, sizeof silent);
    memset(probe_attempts, 0, sizeof probe_attempts);
    const uint8_t channels[] = {1, 6, 11, 1};
    for (int i = 0; i < s_zn; ++i) {
        s_z[i].bond.mac[5] = i;
        s_z[i].bond.last_channel = controller_channel[i] = saved_channel[i] = channels[i];
        s_z[i].synced = true;
        s_z[i].peerVer = SL2_PROTO_VERSION;
        s_z[i].epoch = 42;
        s_z[i].caps.features = SL2_FEAT_LINK_SENSOR | SL2_FEAT_SCREEN;
    }
}
static void run(int selected_screen, uint32_t start) {
    reset(selected_screen, start);
    while ((uint32_t)(clock_ms - start) < 120000) {
        poll_background();
        assert(radio_ch == s_channel && holds == 0);
        clock_ms += 200;
    }
    for (int i = 1; i < s_zn; ++i) {
        if (sensor_count[i] < 4) {
            fprintf(stderr, "FAIL screen=%d zone=%d ch=%d: only %d temperature/humidity reports in 120s\n",
                    screen, i, s_z[i].bond.last_channel, sensor_count[i]);
            exit(1);
        }
        assert(probe_count[i] >= 20);
        assert(max_probe_gap[i] <= 6000); // Survive a lost probe inside controller's 12.5 s window.
        assert(max_sensor_gap[i] <= 30000);
    }
    assert(sensor_count[s_active] == 0); // Background path never impersonates the active path.
    printf("PASS background sensors/keepalive: screen=%d clock_start=%u\n", screen, start);
}
static void scan_reply_commits_channel_without_telemetry(void) {
    reset(0, 5000);
    s_zn = 2;
    scan_zone = 1;
    s_z[1].chMiss = ESPNOW_CHSCAN_MISS_N;
    s_z[1].bond.last_channel = saved_channel[1] = 11;
    s_z[1].sensTxMs = clock_ms;
    s_z[1].sensTxDc = 225;
    s_z[1].scrTxValid = true;
    s_z[1].scrTxOn = false;
    poll_background();
    assert(probe_count[1] == 1 && sensor_count[1] == 0);
    assert(s_z[1].bond.last_channel == 6 && saved_channel[1] == 6);
    assert(s_channel == 1 && radio_ch == 1);
    // Let STATE freshness expire with no second ACK: the confirmed reply
    // must clear the scan miss count, not depend on the zone staying online.
    clock_ms += 4200;
    poll_background();
    assert(probe_count[1] == 2 && sensor_count[1] == 0);
    puts("PASS discovered channel retained without a second telemetry ACK");
}
static void visit_failures_and_delayed_reply(void) {
    for (int mode = 0; mode < 5; ++mode) {
        reset(0, 5000); s_zn = 2;
        if (mode == 0) reply_delay_ms = 300;
        if (mode == 1) reply_delay_ms = UINT32_MAX; // MAC ACK, no application reply.
        if (mode == 2) fail_wake = true;
        if (mode == 3) { fail_tune_channel = 6; fail_tune_count = 1; }
        if (mode == 4) { fail_tune_channel = 1; fail_tune_count = 1; }
        queued_command = true;
        uint32_t before = clock_ms;
        poll_background();
        assert(holds == 0 && radio_ch == s_channel && s_channel == 1);
        assert(clock_ms - before <= ESPNOW_CH_CONFIRM_MS);
        if (mode == 0) assert(clock_ms - before == 300);
        if (mode == 1) assert(clock_ms - before == ESPNOW_CH_CONFIRM_MS);
        if (mode == 2 || mode == 3) assert(probe_count[1] == 0 && sensor_count[1] == 0);
        else assert(probe_count[1] == 1 && sensor_count[1] == 1);
        assert(command_count == 0 && queued_command);
        evt_pump(0); // Main loop resumes queued UI work after the channel visit.
        assert(command_count == 1 && !queued_command);
    }
    puts("PASS bounded reply wait, failed retunes/wake hold, and active command isolation");
}
static void cold_inactive_zone_receives_caps_after_state(void) {
    for (int missing = 0; missing < 2; ++missing) {
        reset(0, 5000); s_zn = 2;
        s_z[1].caps.features = 0;
        s_z[1].want_caps = true;
        // A capability reply from a previous visit cannot satisfy this one.
        s_z[1].lastCapsRxMs = 1;
        s_z[1].lastCapsCh = 6;
        if (missing) caps_delay_ms = UINT32_MAX;
        uint32_t before = clock_ms;
        poll_background();
        assert(radio_ch == s_channel && holds == 0 && sensor_count[1] == 0);
        assert(clock_ms - before == (missing ? ESPNOW_CH_CONFIRM_MS : caps_delay_ms));
        evt_pump(0); // Apply received capabilities only after returning home.
        if (missing) {
            assert(s_z[1].want_caps && s_z[1].caps.features == 0);
            continue;
        }
        clock_ms += 4200;
        poll_background();
        assert(sensor_count[1] == 1 && radio_ch == s_channel && holds == 0);
    }
    puts("PASS cold inactive zone learns capabilities after STATE and sends telemetry");
}
static bool seen_online[4];
static void check_no_stale_flip(void) {
    for (int i = 1; i < s_zn; ++i) {
        if (zone_online(i)) { seen_online[i] = true; continue; }
        if (!seen_online[i]) continue;
        fprintf(stderr, "FAIL zone %d ch %u went from ONLINE to STALE at %u ms "
                        "(STATE %u ms ago, ACK %u ms ago)\n",
                i, s_z[i].bond.last_channel, clock_ms,
                clock_ms - s_z[i].lastStateRxMs,
                s_z[i].lastAckMs ? clock_ms - s_z[i].lastAckMs : 0u);
        exit(1);
    }
}
static void lossy_background_zones_stay_online(void) {
    reset(0, 5000);
    memset(seen_online, 0, sizeof seen_online);
    const uint8_t channels[] = {1, 6, 11, 3};
    reply_delay_ms = 300;
    for (int i = 1; i < s_zn; ++i) {
        s_z[i].bond.last_channel = controller_channel[i] = saved_channel[i] = channels[i];
        /* A controller that takes no dial telemetry: one ACK per visit. */
        s_z[i].caps.features = 0;
        lossy[i] = true;
    }
    uint32_t start = clock_ms;
    while ((uint32_t)(clock_ms - start) < 40000) {
        check_no_stale_flip();
        poll_background();
        assert(radio_ch == s_channel && holds == 0);
        check_no_stale_flip();
        clock_ms += 200;
    }
    for (int i = 1; i < s_zn; ++i) assert(seen_online[i]);
    /* Silence must still take a zone offline once its window runs out. */
    for (int i = 1; i < s_zn; ++i) silent[i] = true;
    uint32_t quiet = clock_ms;
    while ((uint32_t)(clock_ms - quiet) < ESPNOW_ACK_WIN_BG_MS + 2 * BG_PROBE_MS) {
        poll_background();
        clock_ms += 200;
    }
    for (int i = 1; i < s_zn; ++i) assert(!zone_online(i));
    puts("PASS STATE on each visit keeps three lossy background zones online");
}
int main(void) {
    run(SCREEN_ZONES, 1000);
    run(0, 1000);
    run(0, UINT32_MAX - 5000);
    visit_failures_and_delayed_reply();
    scan_reply_commits_channel_without_telemetry();
    cold_inactive_zone_receives_caps_after_state();
    lossy_background_zones_stay_online();
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='serin-background-') as build:
    c = Path(build) / 'test.c'
    c.write_text(prelude + function('static void note_delivery(')
                 + function('static bool zone_online(').replace('zone_online(', 'link_zone_online(', 1)
                 + constants + sensor_constants + function('static bool maybe_send_dial_sensor(')
                 + '\nstatic void poll_background(void) { uint32_t now = now_ms();\n' + block + '\n}\n' + tests)
    binary = Path(build) / 'test'
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-I' + str(root / 'main'), str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
