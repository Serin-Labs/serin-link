# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Run the client's real pairing task against a scripted controller.

pair_run depends on the radio, crypto, UI, and FreeRTOS. Extract it unchanged
and stub those edges. The controller stores the new key before sending
an authenticated PAIR_ACK, so after every successful run the dial's key for
that controller must equal the controller's key, or the zone is stranded.
Also exercise the console pairing command and success-face renderer: during
OTA validation both pairing paths must store the bond and refuse restart.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
ui_source = (root / "main/ui.c").read_text()
source = (root / 'main/espnow_client.c').read_text()


def function(signature, text=source):
    start = text.index(signature)
    end = text.index('\n}\n', start) + 3
    return text[start:end]


pair_state = source[source.index('static uint8_t  s_pair_mac[6];'):
                    source.index('static volatile bool s_ota_hold')]
pair_block = source[source.index('static const uint8_t PAIR_BCAST[6]'):
                    source.index('static void (*volatile s_job_fn)(void *);')]

prelude = r'''
#include <setjmp.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sl2_proto.h"
#include "sl2_pair_auth.h"
#include "espnow_bond.h"
#include "espnow_chscan.h"
#include "link_ota.h"

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_ESPNOW_EXIST 0x3069
#define ESP_MAC_WIFI_STA 0
#define WIFI_IF_STA 0
#define WIFI_SECOND_CHAN_NONE 0
typedef int wifi_second_chan_t;
typedef struct { int unused; } StaticTask_t;
typedef uint8_t StackType_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define pdMS_TO_TICKS(ms) (ms)
typedef struct {
    uint8_t peer_addr[6];
    uint8_t lmk[16];
    uint8_t channel;
    int     ifidx;
    bool    encrypt;
} esp_now_peer_info_t;
typedef enum { PAIR_UI_HIDDEN, PAIR_UI_PROMPT, PAIR_UI_SEARCHING,
               PAIR_UI_OK, PAIR_UI_FAIL, PAIR_UI_FULL,
               PAIR_UI_MISMATCH, PAIR_UI_SAVED } pair_ui_t;

static const char *TAG = "test";
static void log_sink(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGE(tag, ...) log_sink(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) log_sink(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) log_sink(tag, __VA_ARGS__)

static const uint8_t DIAL_MAC[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t UNIT_MAC[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};
static const uint8_t OLD_LMK[16] = {0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1,
                                    0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1};
static const uint8_t NEW_LMK[16] = {0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2,
                                    0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2, 0xB2};
static const uint8_t UNIT_ID[32] = {0x5A};

typedef struct { espnow_bond_rec_t bond; } zone_t;
static zone_t s_z[ESPNOW_MAX_BONDS];
static int s_zn;

static volatile bool s_pairing, s_have_resp, s_pair_cancel;
static struct sl2_pair_resp_pkt s_resp;
static uint8_t s_pair_priv[32], s_pair_pub[32];
static atomic_bool s_offline_alert_reset_due;

static uint32_t clock_ms;
static uint8_t radio_ch = 1;
static uint32_t now_ms(void) { return clock_ms; }
static inline uint32_t ms_stamp(uint32_t now) { return now ? now : 1; }
static void deliver_pending_ack(void);
static void vTaskDelay(uint32_t ms) { clock_ms += ms; deliver_pending_ack(); }
static unsigned uxTaskGetStackHighWaterMark(void *task) { (void)task; return 0; }
static jmp_buf restart_jmp;
static void esp_restart(void) { longjmp(restart_jmp, 1); }
static bool ensure_radio(void) { return true; }
static void esp_read_mac(uint8_t mac[6], int type) { (void)type; memcpy(mac, DIAL_MAC, 6); }
static esp_err_t esp_wifi_set_channel(uint8_t ch, int second) {
    (void)second; radio_ch = ch; return ESP_OK;
}
static esp_err_t esp_wifi_get_channel(uint8_t *ch, wifi_second_chan_t *second) {
    *ch = radio_ch; *second = 0; return ESP_OK;
}
static bool lvgl_port_lock(int timeout) { (void)timeout; return true; }
static void lvgl_port_unlock(void) {}
static pair_ui_t ui_state;
static uint32_t terminal_ui_ms;
static void ui_set_pairing(pair_ui_t state, uint8_t channel) {
    (void)channel; ui_state = state;
    if (state == PAIR_UI_OK || state == PAIR_UI_FAIL || state == PAIR_UI_PROMPT)
        terminal_ui_ms = now_ms();
}
static bool soaking;
bool link_ota_soaking(void) { return soaking; }
bool link_ota_refuse_restart(void) {
    if (!soaking) return false;
    /* The helper takes over the UI; the pairing caller must restore it. */
    ui_state = PAIR_UI_HIDDEN;
    return true;
}
static char console_output[512];
static int capture_printf(const char *fmt, ...) {
    va_list args; va_start(args, fmt);
    size_t used = strlen(console_output);
    int n = vsnprintf(console_output + used, sizeof console_output - used, fmt, args);
    va_end(args); return n;
}
#define printf capture_printf
static void prefs_set_u8(const char *key, uint8_t v) { (void)key; (void)v; }

static bool link_crypto_identity(uint8_t priv[64], uint8_t pub[32]) {
    memset(priv, 0x11, 64); memset(pub, 0x22, 32); return true;
}
static int link_crypto_x25519_keypair(uint8_t priv[32], uint8_t pub[32]) {
    memset(priv, 0x33, 32); memset(pub, 0x44, 32); return 0;
}
static int link_crypto_sign(const uint8_t *priv, const uint8_t *msg, size_t len, uint8_t sig[64]) {
    (void)priv; (void)msg; (void)len; memset(sig, 0x55, 64); return 0;
}
static int link_crypto_verify(const uint8_t *pub, const uint8_t *msg, size_t len,
                              const uint8_t *sig) {
    (void)pub; (void)msg; (void)len; (void)sig; return 0;
}
static int link_crypto_derive_lmk(const uint8_t *priv, const uint8_t *peer,
                                  const uint8_t *a, const uint8_t *b, uint8_t out[16]) {
    (void)priv; (void)peer; (void)a; (void)b; memcpy(out, NEW_LMK, 16); return 0;
}

/* The radio driver's peer table. */
static struct { bool used; uint8_t mac[6]; uint8_t lmk[16]; } peers[8];
static int peer_slot(const uint8_t mac[6]) {
    for (int i = 0; i < 8; i++)
        if (peers[i].used && memcmp(peers[i].mac, mac, 6) == 0) return i;
    return -1;
}
static bool esp_now_is_peer_exist(const uint8_t mac[6]) { return peer_slot(mac) >= 0; }
static esp_err_t esp_now_add_peer(const esp_now_peer_info_t *p) {
    if (peer_slot(p->peer_addr) >= 0) return ESP_ERR_ESPNOW_EXIST;
    for (int i = 0; i < 8; i++) {
        if (peers[i].used) continue;
        peers[i].used = true;
        memcpy(peers[i].mac, p->peer_addr, 6);
        memcpy(peers[i].lmk, p->lmk, 16);
        return ESP_OK;
    }
    return -1;
}
static esp_err_t esp_now_mod_peer(const esp_now_peer_info_t *p) {
    int i = peer_slot(p->peer_addr);
    if (i < 0) return -1;
    memcpy(peers[i].lmk, p->lmk, 16);
    return ESP_OK;
}
static esp_err_t esp_now_del_peer(const uint8_t mac[6]) {
    int i = peer_slot(mac);
    if (i < 0) return -1;
    peers[i].used = false;
    return ESP_OK;
}

/* Controller script. */
static bool controller_acks, cancel_on_confirm, controller_has_key;
static bool confirm_radio_ack, pending_ack, confirm_seen;
static uint32_t ack_delay_ms, pending_ack_ms, cancel_ms;
static uint8_t controller_lmk[16];
static int bond_add_result, bond_adds;
static uint8_t bond_added_lmk[16];
int espnow_bond_add(const espnow_bond_rec_t *r) {
    bond_adds++;
    memcpy(bond_added_lmk, r->lmk, 16);
    return bond_add_result;
}
'''

link_send = r'''
static void deliver_pending_ack(void) {
    if (!pending_ack || now_ms() < pending_ack_ms) return;
    pending_ack = false;
    struct sl2_pair_auth_pkt ack;
    sl2_pair_auth_make(&ack, SL2_PKT_PAIR_ACK, NEW_LMK, DIAL_MAC, UNIT_MAC);
    /* As on_recv: accept only the expected proof while confirming. */
    if (s_pair_confirm && memcmp(UNIT_MAC, s_pair_mac, 6) == 0 &&
        sl2_pair_auth_matches((const uint8_t *)&ack, sizeof ack, &s_pair_expected_ack))
        s_pair_ack_ms = ms_stamp(now_ms());
}

static bool link_send(const uint8_t mac[6], const void *buf, size_t len, const char *what) {
    (void)len; (void)what;
    const uint8_t type = ((const uint8_t *)buf)[0];
    if (type == SL2_PKT_PAIR_REQ) {
        memset(&s_resp, 0, sizeof s_resp);
        s_resp.type = SL2_PKT_PAIR_RESP;
        s_resp.version = SL2_PAIR_AUTH_MIN_VER;
        memcpy(s_resp.src_mac, UNIT_MAC, 6);
        memcpy(s_resp.id_pub, UNIT_ID, 32);
        s_resp.channel = radio_ch;
        s_have_resp = true;
        return true;
    }
    if (type != SL2_PKT_PAIR_CONFIRM || memcmp(mac, UNIT_MAC, 6) != 0) return true;
    if (controller_acks && !confirm_seen) {
        /* The controller stores the new key, then answers. */
        memcpy(controller_lmk, NEW_LMK, 16);
        controller_has_key = true;
        pending_ack_ms = now_ms() + ack_delay_ms;
        pending_ack = true;
        deliver_pending_ack();
    }
    confirm_seen = true;
    if (cancel_on_confirm && !s_pair_cancel) {
        cancel_ms = now_ms();
        s_pair_cancel = true;
    }
    return confirm_radio_ack;
}
'''

pair_face_edges = r'''
/* Render the real pairing face, stubbing only LVGL drawing/layout edges. */
typedef struct { const char *text; bool hidden; } label_t;
static label_t title, hint;
static struct {
    label_t *pairBox, *pairTitle, *offIcon, *pairIcon, *pairInstr, *pairHint,
            *pairBtnLbl, *pairRow, *pairBtn, *pairBtn2, *nolinkRing;
} T = { .pairTitle = &title, .pairHint = &hint };
static pair_ui_t s_pair_state;
static struct { bool bonded; } s_link;
static struct { uint32_t col; } s_arim_home;
static int hero_alert, set_link_row, s_pairbtn_tf, s_pairbtn2_tf;
enum { COL_GREEN, COL_ALERT_NOLINK, COL_LINK, COL_WHITE, COL_HEAT_GLOW,
       COL_ON_ACCENT, COL_HEAT_BASE, PAIR_ROW_DY_FAIL, PAIR_ROW_DY_SEARCH,
       PAIR_ROW_DY, TF_ROW_PAD, TF_CAP_PILL_ROW, LV_OBJ_FLAG_HIDDEN,
       LV_OPA_COVER, OPA_FILL_NEUTRAL, LV_PART_MAIN, LV_STATE_PRESSED,
       ACK_OPA_CHIP };
#define SERIN_PX(x) (x)
#define lv_color_hex(x) (x)
#define lv_obj_set_style_text_color(a,b,c) ((void)(a),(void)(b),(void)(c))
#define lv_obj_set_style_bg_color(a,b,c) ((void)(a),(void)(b),(void)(c))
#define lv_obj_set_style_bg_opa(a,b,c) ((void)(a),(void)(b),(void)(c))
#define lv_obj_set_style_pad_top(a,b,c) ((void)(a),(void)(b),(void)(c))
#define pair_badge(a,b,c) ((void)(a),(void)(b),(void)(c))
#define pair_pill_geom(a,b) ((void)(a),(void)(b))
#define touch_floor_pair(a,b,c,d,e) ((void)(a),(void)(b),(void)(c),(void)(d),(void)(e))
#define arim_sweep_start(a) ((void)(a))
#define arim_stop(a) ((void)(a))
#define lv_obj_invalidate(a) ((void)(a))
static void lv_obj_remove_flag(label_t *obj, int flag) {
    (void)flag; if (obj) obj->hidden = false;
}
static void lv_obj_add_flag(label_t *obj, int flag) {
    (void)flag; if (obj) obj->hidden = true;
}
static void lv_label_set_text(label_t *obj, const char *text) {
    if (obj) obj->text = text;
}
static bool zones_can_enter(void) { return false; }
static void pair_hint_paint(void) {}
'''

tests = r'''
typedef struct {
    const char *name;
    bool known;          /* re-pair of a zone the dial already holds */
    bool acks;           /* controller answers PAIR_CONFIRM */
    bool cancel;         /* user cancel lands on the first confirmation */
    int  bond_result;    /* espnow_bond_add return */
    bool want_restart;
    pair_ui_t want_ui;
    bool want_peer;      /* dial keeps a driver peer for the controller */
    bool radio_ack;      /* PAIR_CONFIRM receives a radio-level ACK */
    uint32_t ack_delay;  /* authenticated ACK delay after first confirm */
    int want_cancel_ms;  /* time from cancellation to terminal UI, or -1 */
    bool soak;          /* post-update validation still pending */
} scenario_t;

static int failures;
static void fail(const scenario_t *s, const char *why) {
    fprintf(stderr, "FAIL %s: %s\n", s->name, why);
    failures++;
}

static void run(const scenario_t *s) {
    memset(peers, 0, sizeof peers);
    memset(s_z, 0, sizeof s_z);
    s_zn = 0;
    controller_has_key = false;
    if (s->known) {
        s_zn = 1;
        memcpy(s_z[0].bond.mac, UNIT_MAC, 6);
        memcpy(s_z[0].bond.lmk, OLD_LMK, 16);
        memcpy(s_z[0].bond.peer_id_pub, UNIT_ID, 32);
        s_z[0].bond.last_channel = 1;
        esp_now_peer_info_t p; memset(&p, 0, sizeof p);
        memcpy(p.peer_addr, UNIT_MAC, 6); memcpy(p.lmk, OLD_LMK, 16);
        esp_now_add_peer(&p);
        memcpy(controller_lmk, OLD_LMK, 16);
        controller_has_key = true;
    }
    controller_acks = s->acks;
    cancel_on_confirm = s->cancel;
    confirm_radio_ack = s->radio_ack;
    ack_delay_ms = s->ack_delay;
    pending_ack = false; confirm_seen = false; cancel_ms = 0; terminal_ui_ms = 0;
    bond_add_result = s->bond_result;
    bond_adds = 0;
    s_pair_cancel = false; s_pair_ack_ms = 0; s_pair_confirm = false;
    s_pairing = false; s_have_resp = false;
    soaking = s->soak;
    clock_ms = 1000; radio_ch = 1; ui_state = PAIR_UI_HIDDEN;
    volatile bool restarted = false;
    if (setjmp(restart_jmp) == 0) {
        pair_run();
    } else {
        restarted = true;
    }
    if (restarted != s->want_restart)
        fail(s, restarted ? "dial restarted" : "dial did not restart into the new bond");
    if (!restarted && ui_state != s->want_ui) {
        char why[64]; snprintf(why, sizeof why, "pairing face %d, want %d", ui_state, s->want_ui);
        fail(s, why);
    }
    if (restarted) {
        s_pair_state = ui_state;
        refresh_pairing_face();
        if (!title.text || strcmp(title.text, "Paired") != 0 || title.hidden ||
            !hint.text || strcmp(hint.text, "Restarting") != 0 || hint.hidden)
            fail(s, "normal success face did not show the pending restart");
    }
    if (s->soak) {
        if (s_pairing) fail(s, "pairing remained in progress after saving");
        if (esp_now_is_peer_exist(PAIR_BCAST)) fail(s, "broadcast peer was not removed");
        s_pair_state = ui_state;
        refresh_pairing_face();
        if (!title.text || strcmp(title.text, "Paired") != 0 || title.hidden)
            fail(s, "saved pairing was not displayed as Paired");
        if (!hint.text || strcmp(hint.text, "Keep the dial plugged in for 30 seconds") != 0 || hint.hidden)
            fail(s, "pairing face did not explain restart refusal");
        soaking = false;
        refresh_pairing_face();
        if (hint.text && strcmp(hint.text, "Restarting") == 0)
            fail(s, "pairing face promised an unscheduled restart after soak");
    }
    if (s->want_cancel_ms >= 0 &&
        terminal_ui_ms - cancel_ms != (uint32_t)s->want_cancel_ms) {
        char why[96];
        snprintf(why, sizeof why, "cancel resolved after %u ms, want %d ms",
                 (unsigned)(terminal_ui_ms - cancel_ms), s->want_cancel_ms);
        fail(s, why);
    }
    if (s->acks && s->bond_result >= 0 &&
        (bond_adds != 1 || memcmp(bond_added_lmk, NEW_LMK, 16) != 0))
        fail(s, "new key was not committed to the bond table");
    if (!s->acks && bond_adds)
        fail(s, "bond committed without an authenticated ACK");
    int slot = peer_slot(UNIT_MAC);
    if (s->want_peer && slot < 0) fail(s, "driver peer for the controller is gone");
    if (!s->want_peer && slot >= 0) fail(s, "driver peer kept without a zone");
    if (slot >= 0 && !s->acks && memcmp(peers[slot].lmk, OLD_LMK, 16) != 0)
        fail(s, "old driver key was not restored without an authenticated ACK");
    if (slot >= 0 && controller_has_key && memcmp(peers[slot].lmk, controller_lmk, 16) != 0)
        fail(s, memcmp(peers[slot].lmk, OLD_LMK, 16) == 0
                ? "old key restored after the controller stored the new one (zone stranded)"
                : "installed key differs from the controller's key");
    /* A restart reloads zones from the bond table; until then RAM must agree. */
    if (!restarted && s->known && memcmp(s_z[0].bond.lmk, controller_lmk, 16) != 0)
        fail(s, "zone keeps a key the controller no longer holds");
}

int main(void) {
    (void)s_pair_tcb; (void)s_pair_stack;
    const scenario_t cases[] = {
        { "successful re-pair during OTA soak", true, true, false, 0,
          false, PAIR_UI_SAVED, true, true, 0, -1, true },
        { "successful new bond during OTA soak", false, true, false, 1,
          false, PAIR_UI_SAVED, true, true, 0, -1, true },
        { "ACK and cancel in one iteration, re-pair", true,  true,  true,  0,
          true,  PAIR_UI_OK,     true,  true, 0, 0, false },
        { "ACK and cancel in one iteration, new zone", false, true,  true,  1,
          true,  PAIR_UI_OK,     true,  true, 0, 0, false },
        { "ACK, then the bond write fails, re-pair",  true,  true,  false, -2,
          false, PAIR_UI_FAIL,   true,  true, 0, -1, false },
        { "ACK and cancel, then the bond write fails", true, true,  true,  -2,
          false, PAIR_UI_FAIL,   true,  true, 0, 0, false },
        { "ACK, then the table is full, new zone",    false, true,  false, -1,
          false, PAIR_UI_FAIL,   false, true, 0, -1, false },
        { "no ACK before timeout, re-pair",           true,  false, false, 0,
          false, PAIR_UI_FAIL,   true,  true, 0, -1, false },
        { "cancel with no ACK, re-pair",              true,  false, true,  0,
          false, PAIR_UI_PROMPT, true,  true, 0, 250, false },
        { "cancel at C-acked, late authenticated ACK, re-pair", true, true, true, 0,
          true,  PAIR_UI_OK,     true,  true, 240, 240, false },
        { "cancel at C-acked, late authenticated ACK, new zone", false, true, true, 1,
          true,  PAIR_UI_OK,     true,  true, 240, 240, false },
        { "cancel at C-acked, authenticated ACK at 250 ms", true, true, true, 0,
          true,  PAIR_UI_OK,     true,  true, 250, 250, false },
        { "cancel before C-acked, re-pair", true, false, true, 0,
          false, PAIR_UI_PROMPT, true,  false, 0, 0, false },
        { "cancel at C-acked without ACK, new zone", false, false, true, 1,
          false, PAIR_UI_PROMPT, false, true, 0, 250, false },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) run(&cases[i]);
    for (int soak = 0; soak <= 1; soak++) {
        soaking = soak; console_output[0] = '\0'; bond_adds = 0; bond_add_result = 1;
        char *args[] = { "espnow-pair", "20:21:22:23:24:25",
                         "b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2" };
        volatile bool restarted = false;
        volatile int rc = -1;
        if (setjmp(restart_jmp) == 0) rc = cmd_pair(3, args);
        else restarted = true;
        scenario_t s = { .name = soak ? "console pairing during OTA soak"
                                     : "console pairing after OTA soak" };
        if (restarted != !soak) fail(&s, "wrong restart decision");
        if (soak && rc != 0) fail(&s, "saved pairing reported as failed");
        if (bond_adds != 1 || memcmp(bond_added_lmk, NEW_LMK, 16) != 0)
            fail(&s, "new console bond was not stored");
        if (soak && !strstr(console_output, "Keep the dial plugged in for 30 seconds"))
            fail(&s, "console did not explain restart refusal");
        if (soak && strstr(console_output, "restarting"))
            fail(&s, "console promised a refused restart");
    }
    if (failures) return 1;
    puts("PASS pairing keeps confirmed keys, bounds cancellation, and gates OTA restarts");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='serin-pair-') as build:
    unit = Path(build) / 'pair.c'
    binary = Path(build) / 'pair'
    unit.write_text(prelude + pair_state + function('static int zone_by_mac(')
                    + function('static int bond_channels(') + link_send + pair_block
                    + function('static int hexval(') + function('static bool parse_mac(')
                    + function('static bool parse_hex16(') + function('static int cmd_pair(')
                    + pair_face_edges + function('static void refresh_pairing_face(', ui_source)
                    + tests)
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(root / 'main'), str(unit), '-o', str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
