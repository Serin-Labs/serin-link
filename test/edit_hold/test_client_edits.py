# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Exercise the client's real edit callback and packet builder without a radio."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "main/espnow_client.c").read_text()


def function(signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


state = source[source.index("static hvac_state_t s_last_sent;"):
               source.index("#define SYNC_WAIT_MS")]
prelude = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit_hold.h"
#include "sl2_ui_map.h"
#include "ui_dd.h"

static hvac_state_t display;
static uint32_t clock_ms;
static int s_active, s_zn = 1;
static bool s_use_f;
static struct { bool synced; ui_caps_t caps; hvac_state_t st; } s_z[2];
static void sweep_backoff_reset(void) {}
static uint32_t now_ms(void) { return clock_ms; }
void ui_get_state(hvac_state_t *out) { *out = display; }
static const char *TAG = "test";
#define ESP_LOGW(tag, ...) ((void)(tag))
static bool lvgl_port_lock(int t) { (void)t; return true; }
static void lvgl_port_unlock(void) {}
static int replays;
static void cmd_tx(int zi, const struct sl2_cmd_pkt *p) { (void)zi; (void)p; replays++; }
static int unit_posts;
static void cmd_post(int zi, const struct sl2_cmd_pkt *p) {
    (void)zi;
    assert(p->mask == SL2_CM_UNITS &&
           "temperature and fan changes must follow the debounce path");
    unit_posts++;
}
'''
production = "\n".join(function(signature) for signature in (
    "static void cmd_put_temp(",
    "static void cmd_put_band(",
    "static bool edits_pending(",
    "static bool pending_edit_tx(",
    "static void resend_held_edits(int zi) {",
    "void espnow_client_on_local_edit(",
    "void espnow_client_on_units_edit(",
))
tests = (Path(__file__).with_suffix(".c")).read_text()

with tempfile.TemporaryDirectory(prefix="serin-client-edits-") as build:
    unit = Path(build) / "client_edits.c"
    binary = Path(build) / "client_edits"
    unit.write_text(prelude + state + production + tests)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "main"), str(unit), "-lm", "-o", str(binary)],
                   check=True)
    failed = []
    for field in ("temperature", "band", "fan"):
        for end in ("confirmed", "expired", "dropped",
                    "back_within_debounce", "state_then_back", "replay_then_back",
                    "back_after_tx", "units_flip", "units_back", "units_round_trip",
                "units_zone_switch"):
            result = subprocess.run([str(binary), field, end])
            if result.returncode:
                failed.append(f"{field}/{end}")
    if failed:
        raise SystemExit("Client edit regressions failed: " + ", ".join(failed))
