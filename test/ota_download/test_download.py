# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Exercise the real OTA task and downloader against a scripted update server.

Extract both functions unchanged, as in the pairing/background host tests. The
SDK boundaries supply an app descriptor and a parsed manifest; TLS, digest and
signature checks succeed. A successful SDK finish selects the candidate boot
partition. Rejecting a relabeled image must leave that selection unchanged and
must abort before esp_https_ota_perform writes the image. Keeping ota_task real
also catches passing the wrong manifest version to download.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "main/link_ota.c").read_text()


def function(signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


declarations = source[source.index('static const char *TAG ='):
                      source.index("#define VALID_SOAK_MS")]

prelude = r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "ota_ver.h"

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NOT_ALLOWED 0x10D
#define ESP_ERR_NOT_FINISHED 0x10C
#define ESP_ERR_INVALID_VERSION 0x10A
#define ESP_ERR_INVALID_CRC 0x109
#define ESP_ERR_HTTPS_OTA_IN_PROGRESS 1
#define CONFIG_SERIN_BOARD_VIEWE_15 1
#define MALLOC_CAP_SPIRAM 1
#define pdMS_TO_TICKS(ms) (ms)
#define OTA_URL_MAX 192

typedef struct { int slot; } esp_partition_t;
/* ESP-IDF v5.5.4 esp_app_desc_t: keep the complete layout, including the
   adjacent project_name, so unterminated version fields are realistic. */
typedef struct {
    uint32_t magic_word, secure_version, reserv1[2];
    char version[32], project_name[32], time[16], date[16], idf_ver[32];
    uint8_t app_elf_sha256[32];
    uint16_t min_efuse_blk_rev_full, max_efuse_blk_rev_full;
    uint8_t mmu_page_size, reserv3[3];
    uint32_t reserv2[18];
} esp_app_desc_t;
_Static_assert(sizeof(esp_app_desc_t) == 256, "SDK descriptor layout");
typedef struct {
    const char *url;
    void (*crt_bundle_attach)(void);
    int timeout_ms;
    bool keep_alive_enable;
    int buffer_size;
} esp_http_client_config_t;
typedef struct {
    const esp_http_client_config_t *http_config;
    int buffer_caps;
    bool partial_http_download;
    int max_http_request_size;
} esp_https_ota_config_t;
typedef void *esp_https_ota_handle_t;
typedef struct { const char *server; } esp_sntp_config_t;
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(host) ((esp_sntp_config_t){host})
typedef enum { ESPNOW_WIFI_REQ_OK, ESPNOW_WIFI_REQ_NO_CREDS } espnow_wifi_req_t;
typedef enum { OTA_UI_HIDDEN, OTA_UI_CHECKING, OTA_UI_UPTODATE, OTA_UI_CONFIRM,
               OTA_UI_DOWNLOAD, OTA_UI_VERIFY, OTA_UI_OK, OTA_UI_FAIL } ota_ui_t;

/* cJSON's parse boundary supplies the fixed manifest tree below. JSON parsing
   itself is external to the version-policy contract under test. */
typedef struct cJSON {
    struct cJSON *next, *prev, *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;
} cJSON;
#define cJSON_ArrayForEach(item, array) \
    for (item = (array)->child; item != NULL; item = item->next)
static bool cJSON_IsString(const cJSON *j) { return j && j->type == 1; }
static bool cJSON_IsNumber(const cJSON *j) { return j && j->type == 2; }
static const cJSON *cJSON_GetObjectItem(const cJSON *j, const char *key) {
    for (const cJSON *p = j->child; p; p = p->next)
        if (strcmp(p->string, key) == 0) return p;
    return NULL;
}
'''

edges = r'''
static esp_app_desc_t running, incoming;
static esp_partition_t current_partition = {0}, candidate_partition = {1};
static const esp_partition_t *boot_partition;
static bool active, checked_hash, paused;
static int begins, performs, aborts, finishes, restarts;
static int failures, cases;
static ota_ui_t result_ui;
static char failure_reason[80], confirmed_version[80];
static cJSON document, version, builds, build, board, path, hash, size;
static const char *const image_hash =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

static void log_sink(const char *tag, const char *fmt, ...) {
    (void)tag;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
}
#define ESP_LOGE(tag, ...) log_sink(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) log_sink(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) log_sink(tag, __VA_ARGS__)
static const esp_app_desc_t *esp_app_get_description(void) { return &running; }
static void esp_crt_bundle_attach(void) {}
static const esp_partition_t *esp_ota_get_next_update_partition(const void *p) {
    (void)p;
    return &candidate_partition;
}
static esp_err_t esp_https_ota_begin(const esp_https_ota_config_t *c,
                                   esp_https_ota_handle_t *h) {
    assert(strcmp(c->http_config->url, "https://updates.example/stable/link15-app.bin") == 0);
    assert(!active);
    active = true;
    *h = &candidate_partition;
    ++begins;
    return ESP_OK;
}
static esp_err_t esp_https_ota_get_img_desc(esp_https_ota_handle_t h, esp_app_desc_t *d) {
    assert(active && h == &candidate_partition);
    *d = incoming;
    return ESP_OK;
}
static esp_err_t esp_https_ota_abort(esp_https_ota_handle_t h) {
    assert(active && h == &candidate_partition);
    active = false;
    ++aborts;
    return ESP_OK;
}
static esp_err_t esp_https_ota_perform(esp_https_ota_handle_t h) {
    assert(active && h == &candidate_partition);
    ++performs;
    return ESP_OK;
}
static int esp_https_ota_get_image_len_read(esp_https_ota_handle_t h) {
    (void)h;
    return 4096;
}
static bool esp_https_ota_is_complete_data_received(esp_https_ota_handle_t h) {
    assert(active && h == &candidate_partition);
    return performs > 0;
}
static bool sha_check(const esp_partition_t *p, int n, const char *hex) {
    assert(p == &candidate_partition && n == 4096 && strcmp(hex, image_hash) == 0);
    checked_hash = true;
    return true;
}
static esp_err_t esp_https_ota_finish(esp_https_ota_handle_t h) {
    assert(active && h == &candidate_partition && checked_hash);
    active = false;
    boot_partition = &candidate_partition;
    ++finishes;
    return ESP_OK;
}
static void esp_restart(void) { ++restarts; }
static void ota_ui(ota_ui_t state, int pct, const char *txt) {
    (void)pct;
    if (state == OTA_UI_CONFIRM)
        snprintf(confirmed_version, sizeof confirmed_version, "%s", txt);
    if (state == OTA_UI_FAIL)
        snprintf(failure_reason, sizeof failure_reason, "%s", txt);
    if (state == OTA_UI_OK || state == OTA_UI_FAIL || state == OTA_UI_UPTODATE)
        result_ui = state;
}
static void vTaskDelay(unsigned ms) {
    if (ms == 100 && confirmed_version[0]) s_confirm = true;
}
static espnow_wifi_req_t espnow_client_request_wifi(char *ssid, size_t ssid_cap,
                                                   char *psk, size_t psk_cap, int ms) {
    (void)ms;
    snprintf(ssid, ssid_cap, "test-wifi");
    snprintf(psk, psk_cap, "test-password");
    return ESPNOW_WIFI_REQ_OK;
}
static void espnow_client_ota_pause(bool pause) { paused = pause; }
static bool wifi_sta_join(const char *ssid, const char *psk, volatile bool *cancel) {
    (void)ssid; (void)psk; (void)cancel;
    return true;
}
static void esp_wifi_disconnect(void) {}
static void esp_netif_sntp_init(const esp_sntp_config_t *sc) { (void)sc; }
static esp_err_t esp_netif_sntp_sync_wait(int ms) { (void)ms; return ESP_OK; }
static void esp_netif_sntp_deinit(void) {}
static const char *ota_manifest_url(char *buf, size_t cap) {
    snprintf(buf, cap, "https://updates.example/stable/manifest.json");
    return buf;
}
static char *fetch_manifest(void) { return strdup("manifest fixture"); }
static cJSON *cJSON_Parse(const char *text) {
    assert(strcmp(text, "manifest fixture") == 0);
    return &document;
}
static void cJSON_Delete(cJSON *j) { assert(j == &document); }
'''

tests = r'''
static void prepare(const char *local, const char *offered, const char *image) {
    running = (esp_app_desc_t){.project_name = "serin_link_viewe15"};
    incoming = running;
    /* A 32-byte fixture intentionally fills the version field without a NUL. */
    size_t n = strlen(local);
    memcpy(running.version, local, n < sizeof running.version ? n : sizeof running.version);
    n = strlen(image);
    memcpy(incoming.version, image, n < sizeof incoming.version ? n : sizeof incoming.version);
    document = (cJSON){.child = &version};
    version = (cJSON){.string = "version", .type = 1, .valuestring = (char *)offered, .next = &builds};
    builds = (cJSON){.string = "builds", .child = &build};
    build = (cJSON){.child = &board};
    board = (cJSON){.string = "board", .type = 1, .valuestring = "link15", .next = &path};
    path = (cJSON){.string = "path", .type = 1, .valuestring = "link15-app.bin", .next = &hash};
    hash = (cJSON){.string = "sha256", .type = 1, .valuestring = (char *)image_hash, .next = &size};
    size = (cJSON){.string = "size", .type = 2, .valueint = 4096, .valuedouble = 4096};
    boot_partition = &current_partition;
    begins = performs = aborts = finishes = restarts = 0;
    s_cancel = s_confirm = active = checked_hash = paused = false;
    s_running = true;
    result_ui = OTA_UI_HIDDEN;
    failure_reason[0] = confirmed_version[0] = '\0';
}

static void check(bool ok, const char *name) {
    ++cases;
    if (ok) return;
    ++failures;
    fprintf(stderr, "FAIL %s: begin=%d perform=%d abort=%d finish=%d restart=%d reason=\"%s\"\n",
            name, begins, performs, aborts, finishes, restarts, failure_reason);
}

static void rejected(const char *name, const char *local, const char *offered,
                     const char *image) {
    prepare(local, offered, image);
    ota_task(NULL);
    check(begins == 1 && aborts == 1 && performs == 0 && finishes == 0 && restarts == 0 &&
          boot_partition == &current_partition && !active && !paused && !s_running &&
          result_ui == OTA_UI_FAIL && strcmp(failure_reason, "Wrong image version") == 0 &&
          strcmp(confirmed_version, offered) == 0, name);
}

static void accepted(const char *name, const char *local, const char *offered,
                     const char *image) {
    prepare(local, offered, image);
    ota_task(NULL);
    check(begins == 1 && aborts == 0 && performs > 0 && finishes == 1 && restarts == 1 &&
          boot_partition == &candidate_partition && !active && !paused && !s_running &&
          result_ui == OTA_UI_OK && strcmp(confirmed_version, offered) == 0, name);
}

static void direct_rejection(const char *name, const char *local, const char *image) {
    prepare(local, image, image);
    esp_err_t err = download("https://updates.example/stable/link15-app.bin", 4096, image_hash, image);
    check(err == OTA_ERR_WRONG_VERSION && aborts == 1 && performs == 0 && finishes == 0 &&
          boot_partition == &current_partition && !active, name);
}

static void bad_manifest(const char *offered) {
    prepare("0.2.0", offered, offered);
    ota_task(NULL);
    check(begins == 0 && finishes == 0 && restarts == 0 && boot_partition == &current_partition &&
          result_ui == OTA_UI_FAIL && strcmp(failure_reason, "Bad manifest") == 0,
          "malformed manifest version is rejected before confirmation");
}

int main(void) {
    rejected("older signed image relabeled as a newer release", "0.2.0", "0.3.0", "0.1.10");
    rejected("running image relabeled as a newer release", "0.2.0", "0.3.0", "0.2.0");
    rejected("newer image differs from the offered version", "0.2.0", "0.4.0", "0.3.0");
    rejected("equal prerelease ordering does not alias a different label", "0.2.0", "0.3.0-beta.1", "0.3.0-rc.1");
    rejected("incoming v prefix does not alias the manifest", "0.2.0", "0.3.0", "v0.3.0");
    rejected("incoming leading zero does not alias the manifest", "0.2.0", "0.3.0", "00.3.0");
    rejected("empty image version", "0.2.0", "0.3.0", "");
    rejected("malformed image version", "0.2.0", "0.3.0", "not-a-version");
    rejected("non-ASCII image version", "0.2.0", "0.3.0", "0.3.0\xff");
    rejected("unterminated descriptor version", "0.2.0", "0.3.0", "33333333333333333333333333333333");
    accepted("matching newer release", "0.2.0", "0.3.0", "0.3.0");
    accepted("matching leading-zero release remains supported", "0.2.0", "00.3.0", "00.3.0");
    accepted("matching later prerelease number", "0.3.0-beta.9", "0.3.0-beta.10", "0.3.0-beta.10");
    accepted("stable release follows its prerelease", "0.3.0-beta.3", "0.3.0", "0.3.0");
    accepted("higher core prerelease follows stable", "0.3.0", "0.4.0-beta.1", "0.4.0-beta.1");
    accepted("prefixed running bench version remains compatible", "v0.2.0", "0.3.0", "0.3.0");
    accepted("stable release follows a running git-describe build", "0.3.0-3-gabc1234", "0.3.0", "0.3.0");
    accepted("large core component stays newer", "0.2.0", "2147483648.0.0", "2147483648.0.0");
    accepted("large prerelease number stays below stable", "0.3.0-beta.2147483648", "0.3.0", "0.3.0");
    accepted("largest descriptor version remains valid", "0.2.0", "999999999999999999999999999.0.0", "999999999999999999999999999.0.0");
    direct_rejection("download independently refuses the running version", "0.3.0", "0.3.0");
    direct_rejection("download independently refuses an older version", "0.3.0", "0.2.0");
    direct_rejection("download refuses a prerelease of the running release", "0.3.0", "0.3.0-beta.10");
    direct_rejection("download refuses an older prerelease number", "0.3.0-beta.3", "0.3.0-beta.2");
    direct_rejection("download preserves equal prerelease-label ordering", "0.3.0-rc.1", "0.3.0-beta.1");
    direct_rejection("unterminated running descriptor", "22222222222222222222222222222222", "0.3.0");
    direct_rejection("unparseable running descriptor", "garbage", "0.3.0");
    const char *malformed[] = {
        "", "1.2", "1.2.3tail", "1.2.3-beta", "1.2.3-beta.1-dirty", "1.2.3-1.2",
        "1.2.3-beta.-1", "v1.2.3", "V1.2.3", "+1.2.3", "-1.2.3", " 1.2.3",
        "1.2.3+build.1", "1.2.3-\xff.1", "33333333333333333333333333333333",
    };
    for (size_t i = 0; i < sizeof malformed / sizeof malformed[0]; ++i) {
        direct_rejection("malformed matching image/manifest version", "0.2.0", malformed[i]);
        bad_manifest(malformed[i]);
    }
    prepare("0.2.0", "0.3.0", "0.3.0");
    strcpy(incoming.project_name, "serin_link");
    ota_task(NULL);
    check(finishes == 1 && boot_partition == &candidate_partition, "legacy board-name migration remains allowed");
    prepare("0.2.0", "0.3.0", "0.3.0");
    strcpy(incoming.project_name, "serin_link_viewe21");
    ota_task(NULL);
    check(aborts == 1 && performs == 0 && finishes == 0 && boot_partition == &current_partition &&
          strcmp(failure_reason, "Wrong board image") == 0, "other board remains rejected");
    printf("ota_download: %d/%d passed\n", cases - failures, cases);
    return failures ? 1 : 0;
}
'''

with tempfile.TemporaryDirectory(prefix="serin-ota-download-") as build_dir:
    unit = Path(build_dir) / "download.c"
    binary = Path(build_dir) / "download"
    unit.write_text(prelude + declarations + edges + function("static esp_err_t download(")
                    + function("static void ota_task(") + tests)
    subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "main"), str(unit), "-o", str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)]).returncode)
