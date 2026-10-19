# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Exercise the real probation task, init, and restart gate with SDK edges.

Simulated time advances only at vTaskDelay; link and first-frame observations
follow each scenario. SDK refusal preserves PENDING_VERIFY, matching the local
IDF path that rejects rollback before writing OTA metadata. Flash errors have
no such persistence guarantee and are checked only for absence of acceptance.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "main/link_ota.c").read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


constants = source[source.index("#define VALID_SOAK_MS"):
                   source.index("static void ota_guard_task(")]
prelude = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ota_ver.h"
#include "link_ota.h"
typedef int esp_err_t;
typedef enum { ESP_OTA_IMG_PENDING_VERIFY, ESP_OTA_IMG_VALID } esp_ota_img_states_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_OTA_ROLLBACK_FAILED 0x1505
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
#define OTA_UI_FAIL 7
static const char *TAG = "test";
static void ota_guard_task(void *arg);
static void log_sink(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
#define ESP_LOGI(tag, ...) log_sink(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) log_sink(tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) log_sink(tag, __VA_ARGS__)
'''
edges = r'''
static int64_t now_ms, link_at, frame_at, drop_at, recover_at, stall_at, stall_ms;
static int valid_calls, rollback_calls, creates, deletes, valid_failures;
static int64_t accepted_at, rollback_at;
static bool allocation_fails;
static esp_err_t rollback_result;
static esp_ota_img_states_t metadata;
static void (*created_task)(void *);
static jmp_buf reboot;
static const char *hint;
static int cases, failures;

static int64_t esp_timer_get_time(void) { return now_ms * 1000; }
static bool espnow_client_link_alive(void) {
    return now_ms >= link_at && !(now_ms >= drop_at && now_ms < recover_at);
}
static bool ui_has_rendered(void) { return now_ms >= frame_at; }
static void vTaskDelay(unsigned ms) {
    now_ms += now_ms == stall_at ? stall_ms : ms;
    assert(now_ms <= 602000); /* A retry must not extend the boot deadline. */
}
static unsigned uxTaskGetStackHighWaterMark(void *task) { (void)task; return 1024; }
static void vTaskDelete(void *task) { (void)task; ++deletes; }
static esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {
    ++valid_calls;
    if (valid_failures) { --valid_failures; return ESP_FAIL; }
    metadata = ESP_OTA_IMG_VALID;
    accepted_at = now_ms;
    return ESP_OK;
}
static esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) {
    ++rollback_calls;
    rollback_at = now_ms;
    if (rollback_result == ESP_OK) longjmp(reboot, 1); /* SDK success reboots. */
    return rollback_result;
}
static const char *esp_err_to_name(esp_err_t err) { (void)err; return "scripted"; }
static const void *esp_ota_get_running_partition(void) { return &metadata; }
static esp_err_t esp_ota_get_state_partition(const void *partition,
                                            esp_ota_img_states_t *state) {
    assert(partition == &metadata);
    *state = metadata;
    return ESP_OK;
}
static int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                       void *arg, unsigned priority, void *handle) {
    (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    ++creates;
    if (allocation_fails) return 0;
    created_task = task;
    return pdPASS;
}
static void ota_ui(int state, int pct, const char *text) {
    assert(state == OTA_UI_FAIL && pct == 0);
    hint = text;
}
'''
tests = r'''
static void prepare(void) {
    now_ms = link_at = frame_at = 0;
    drop_at = recover_at = stall_at = INT64_MAX;
    stall_ms = 0;
    valid_calls = rollback_calls = creates = deletes = valid_failures = 0;
    accepted_at = rollback_at = -1;
    allocation_fails = false;
    rollback_result = ESP_OK;
    metadata = ESP_OTA_IMG_PENDING_VERIFY;
    created_task = NULL;
    hint = NULL;
}
static void check(bool condition, const char *name) {
    ++cases;
    if (condition) return;
    ++failures;
    fprintf(stderr, "FAIL %s: accepted=%lld valid_calls=%d rollback=%lld calls=%d state=%d\n",
            name, (long long)accepted_at, valid_calls, (long long)rollback_at,
            rollback_calls, metadata);
}
static void run(void) {
    if (setjmp(reboot)) return;
    link_ota_init();
    if (created_task) created_task(NULL);
}
int main(void) {
    prepare(); run();
    check(accepted_at == 30000 && valid_calls == 1 && rollback_calls == 0,
          "30 seconds of observed health accepts the image");
    check(!link_ota_soaking() && !link_ota_refuse_restart() && !hint,
          "successful validation releases the restart gate");

    prepare(); link_at = 120000; run();
    check(accepted_at == 150000 && rollback_calls == 0,
          "late link must complete its own 30 second window");
    prepare(); link_at = 32000; drop_at = 34000; run();
    check(valid_calls == 0 && rollback_at == 600000,
          "one transient healthy poll after boot cannot accept");
    prepare(); drop_at = 20000; recover_at = 22000; run();
    check(accepted_at == 52000, "unhealthy poll resets the entire health window");
    prepare(); frame_at = 60000; run();
    check(accepted_at == 60000, "first frame is required, independently of link window");
    prepare(); frame_at = INT64_MAX; run();
    check(valid_calls == 0 && rollback_at == 600000, "no first frame reaches deadline");
    prepare(); link_at = INT64_MAX; run();
    check(valid_calls == 0 && rollback_at == 600000, "no link reaches deadline");
    prepare(); link_at = 580000; run();
    check(valid_calls == 0 && rollback_at == 600000, "late window cannot extend deadline");
    prepare(); link_at = 570000; run();
    check(valid_calls == 0 && rollback_at == 600000, "deadline takes priority over acceptance");
    prepare(); stall_at = 10000; stall_ms = 30000; run();
    check(accepted_at == 70000, "unobserved task stall starts a fresh window");
    prepare(); stall_at = 10000; stall_ms = 4000; run();
    check(accepted_at == 30000, "one delayed poll within tolerance keeps window");

    prepare(); valid_failures = 3; run();
    check(valid_calls == 4 && accepted_at == 36000 && rollback_calls == 0,
          "mark valid errors retry instead of ending guard");
    prepare(); valid_failures = 1000; run();
    check(valid_calls == 285 && accepted_at == -1 && rollback_at == 600000,
          "persistent mark valid errors roll back at bounded deadline");
    prepare(); link_at = 568000; valid_failures = 1; run();
    check(valid_calls == 1 && accepted_at == -1 && rollback_at == 600000,
          "mark valid retry cannot run at or beyond deadline");
    prepare(); valid_failures = 1; drop_at = 32000; recover_at = 34000; run();
    check(valid_calls == 2 && accepted_at == 64000,
          "health loss after mark valid error requires a fresh window");

    prepare(); link_at = INT64_MAX; rollback_result = ESP_ERR_OTA_ROLLBACK_FAILED; run();
    check(valid_calls == 0 && rollback_calls == 1 && deletes == 1 && link_ota_soaking(),
          "rollback refusal never accepts or clears pending state");
    check(link_ota_refuse_restart() && hint && strcmp(hint, LINK_OTA_SOAK_MSG) == 0,
          "restart gate remains active after rollback refusal");
    prepare(); link_at = INT64_MAX; rollback_result = ESP_FAIL; run();
    check(valid_calls == 0 && rollback_calls == 1, "rollback write error never accepts");
    prepare(); allocation_fails = true; run();
    check(creates == 1 && valid_calls == 0 && rollback_calls == 1 && rollback_at == 0,
          "allocation failure attempts recovery without accepting");
    prepare(); allocation_fails = true; rollback_result = ESP_ERR_OTA_ROLLBACK_FAILED; run();
    check(valid_calls == 0 && rollback_calls == 1 && link_ota_soaking() && link_ota_refuse_restart(),
          "allocation and rollback refusal retain probation and restart gate");
    prepare(); metadata = ESP_OTA_IMG_VALID; run();
    check(creates == 0 && valid_calls == 0 && rollback_calls == 0,
          "already valid firmware does not start probation");
    printf("ota_guard: %d/%d passed\n", cases - failures, cases);
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="serin-ota-guard-") as build_dir:
    unit = Path(build_dir) / "guard.c"
    binary = Path(build_dir) / "guard"
    unit.write_text(prelude + constants + edges + function("static void ota_guard_task(")
                    + function("bool link_ota_soaking(") + function("void link_ota_init(")
                    + function("bool link_ota_refuse_restart(") + tests)
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "main"), str(unit), "-o", str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
