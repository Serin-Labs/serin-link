/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "link_ota.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"
#include "esp_lvgl_port.h"
#include "sdkconfig.h"
#include "ui.h"
#include "espnow_client.h"
#include "esp_console.h"
#include "prefs.h"
#include "ota_ver.h"
#include "wifi_sta.h"

static const char *TAG = "link_ota";

#if CONFIG_SERIN_BOARD_VIEWE_15
#define LINK_BOARD_ID "link15"
#else
#define LINK_BOARD_ID "link21"
#endif

#define CREDS_TIMEOUT_MS   5000
#define SNTP_TIMEOUT_MS    2000
#define CONFIRM_TIMEOUT_MS 60000
#define MANIFEST_MAX       4096
#define MANIFEST_MAX_REDIRECTS 4
#define OTA_HTTP_BUF       4096

static volatile bool s_running;
static volatile bool s_cancel;
static volatile bool s_confirm;

#define VALID_SOAK_MS      30000
#define VALID_POLL_MS      2000
#define VALID_DEADLINE_MS  (10 * 60 * 1000)
#define VALID_TASK_STACK   3072

static void ota_guard_task(void *arg)
{
    (void)arg;
    for (;;) {
        int64_t up_ms = esp_timer_get_time() / 1000;
        bool link  = espnow_client_link_alive();
        bool drawn = ui_has_rendered();
        ota_guard_act_t act = ota_guard_decide(up_ms, link && drawn,
                                               VALID_SOAK_MS, VALID_DEADLINE_MS);
        if (act == OTA_GUARD_VALID) {
            esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI(TAG, "OTA image marked valid at %d s up (%s)",
                     (int)(up_ms / 1000), esp_err_to_name(e));
            break;
        }
        if (act == OTA_GUARD_ROLLBACK) {
            ESP_LOGE(TAG, "health gate never passed in %d s (link=%d ui=%d) — rolling back",
                     (int)(up_ms / 1000), (int)link, (int)drawn);
            esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();
            ESP_LOGE(TAG, "rollback refused (%s) — keeping this image",
                     esp_err_to_name(e));
            esp_ota_mark_app_valid_cancel_rollback();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(VALID_POLL_MS));
    }
    ESP_LOGI(TAG, "ota_guard: stack high-water %u free of %u",
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)VALID_TASK_STACK);
    vTaskDelete(NULL);
}

void link_ota_init(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(run, &st) != ESP_OK ||
        st != ESP_OTA_IMG_PENDING_VERIFY)
        return;

    if (xTaskCreate(ota_guard_task, "ota_guard", VALID_TASK_STACK, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "rollback-guard task create failed — marking valid now");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

static void ota_ui(ota_ui_t st, int pct, const char *txt)
{
    if (lvgl_port_lock(0)) { ui_set_ota(st, pct, txt); lvgl_port_unlock(); }
}

#define OTA_BETA_PREF  "ota_beta"
#define OTA_URL_PREF   "ota_url"
#define OTA_URL_B_PREF "ota_url_b"
#define OTA_URL_MAX    192

static bool ota_beta_available(void)
{
    return CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL[0] != '\0';
}

/* A dial installed from a pre-release image follows the beta feed until told
   otherwise. On the stable feed it would be offered the older stable release
   as an update, because a release outranks its own pre-releases. */
static bool ota_beta_selected(void)
{
    bool pre = ota_ver_is_prerelease(esp_app_get_description()->version);
    return ota_beta_available() && prefs_get_bool(OTA_BETA_PREF, pre);
}

/* Caller supplies the buffer: this runs on both the console and the ESP-NOW
   worker task, so static storage would race. */
static const char *ota_manifest_url(char *buf, size_t cap)
{
    if (ota_beta_selected())
        prefs_get_str(OTA_URL_B_PREF, buf, cap, CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL);
    else
        prefs_get_str(OTA_URL_PREF, buf, cap, CONFIG_SERIN_LINK_OTA_MANIFEST_URL);
    return buf;
}

static int cmd_ota_channel(int argc, char **argv)
{
    if (argc >= 2) {
        if (strcmp(argv[1], "beta") == 0) {
            if (!ota_beta_available()) { printf("ota-channel: no beta channel in this build\n"); return 1; }
            prefs_set_bool(OTA_BETA_PREF, true);
        } else if (strcmp(argv[1], "stable") == 0) {
            /* Stored, not erased: on a pre-release build an absent pref means beta. */
            prefs_set_bool(OTA_BETA_PREF, false);
        } else {
            printf("usage: ota-channel [stable|beta]\n");
            return 1;
        }
    }
    char url[OTA_URL_MAX];
    printf("ota-channel: %s\n  %s\n",
           ota_beta_selected() ? "beta" : "stable",
           ota_manifest_url(url, sizeof url));
    return 0;
}

static void ota_url_show(const char *chan, const char *key, const char *def)
{
    char url[OTA_URL_MAX];
    bool nvs = prefs_get_str(key, url, sizeof url, "") > 0;
    printf("  %-6s %s (%s)\n", chan, nvs ? url : (def[0] ? def : "none"),
           nvs ? "nvs" : "built-in");
}

/* An http:// override still needs CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP, which the
   default build does not set — it resolves here but fails at download time. */
static int cmd_ota_url(int argc, char **argv)
{
    if (argc >= 2) {
        const char *key = ota_beta_selected() ? OTA_URL_B_PREF : OTA_URL_PREF;
        if (strcmp(argv[1], "default") == 0) {
            prefs_erase(key);
        } else if ((strncmp(argv[1], "https://", 8) == 0 ||
                    strncmp(argv[1], "http://", 7) == 0) &&
                   strlen(argv[1]) < OTA_URL_MAX) {
            prefs_set_str(key, argv[1]);
        } else {
            printf("usage: ota-url [<http(s):// url, under %d chars>|default]\n", OTA_URL_MAX);
            return 1;
        }
    }
    printf("ota-url:\n");
    ota_url_show("stable", OTA_URL_PREF,   CONFIG_SERIN_LINK_OTA_MANIFEST_URL);
    ota_url_show("beta",   OTA_URL_B_PREF, CONFIG_SERIN_LINK_OTA_MANIFEST_BETA_URL);
    return 0;
}

void link_ota_register_console(void)
{
    const esp_console_cmd_t c = {
        .command = "ota-channel",
        .help    = "Show or set the update channel: ota-channel [stable|beta]",
        .func    = cmd_ota_channel,
    };
    esp_console_cmd_register(&c);
    const esp_console_cmd_t u = {
        .command = "ota-url",
        .help    = "Show update URLs, or override the selected channel: ota-url [<url>|default]",
        .func    = cmd_ota_url,
    };
    esp_console_cmd_register(&u);
}

static char *fetch_manifest(void)
{
    char url[OTA_URL_MAX];
    esp_http_client_config_t hc = {
        .url = ota_manifest_url(url, sizeof url),
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&hc);
    if (!cl) return NULL;
    char *buf = NULL;
    for (int hop = 0; hop <= MANIFEST_MAX_REDIRECTS; hop++) {
        if (esp_http_client_open(cl, 0) != ESP_OK) break;
        esp_http_client_fetch_headers(cl);
        int status = esp_http_client_get_status_code(cl);
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            char sink[128];
            while (esp_http_client_read(cl, sink, sizeof sink) > 0) { }
            if (esp_http_client_set_redirection(cl) != ESP_OK) {
                ESP_LOGE(TAG, "manifest HTTP %d with no usable Location", status);
                break;
            }
            continue;
        }
        if (status != 200) { ESP_LOGE(TAG, "manifest HTTP %d", status); break; }
        buf = malloc(MANIFEST_MAX);
        if (buf) {
            int n = 0, r;
            while (n < MANIFEST_MAX - 1 &&
                   (r = esp_http_client_read(cl, buf + n, MANIFEST_MAX - 1 - n)) > 0)
                n += r;
            buf[n] = '\0';
        }
        break;
    }
    if (!buf) ESP_LOGE(TAG, "manifest fetch failed");
    esp_http_client_cleanup(cl);
    return buf;
}

static bool sha_check(const esp_partition_t *part, int len, const char *hex)
{
    if (!part || len <= 0 || !hex || strlen(hex) != 64) return false;
    uint8_t want[32];
    for (int i = 0; i < 32; i++) {
        int hi = ota_hexval(hex[2 * i]), lo = ota_hexval(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        want[i] = (uint8_t)((hi << 4) | lo);
    }
    uint8_t *buf = malloc(4096);
    if (!buf) return false;
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    for (int off = 0; off < len; off += 4096) {
        int n = len - off < 4096 ? len - off : 4096;
        if (esp_partition_read(part, off, buf, n) != ESP_OK) { free(buf); mbedtls_sha256_free(&c); return false; }
        mbedtls_sha256_update(&c, buf, n);
    }
    uint8_t got[32];
    mbedtls_sha256_finish(&c, got);
    mbedtls_sha256_free(&c);
    free(buf);
    bool ok = memcmp(want, got, 32) == 0;
    if (!ok) ESP_LOGE(TAG, "image SHA256 mismatch");
    return ok;
}

static esp_err_t download(const char *fw_url, int plain_size, const char *sha_hex)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    esp_http_client_config_t hc = {
        .url = fw_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .buffer_size = OTA_HTTP_BUF,
    };
    esp_https_ota_config_t oc = {
        .http_config           = &hc,
        .buffer_caps           = MALLOC_CAP_SPIRAM,
        .partial_http_download = true,
        .max_http_request_size = 16384,
    };
    esp_https_ota_handle_t h = NULL;
    esp_err_t err = esp_https_ota_begin(&oc, &h);
    if (err != ESP_OK) return err;
    int last_pct = -1;
    while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (s_cancel) {
            esp_https_ota_abort(h);
            return ESP_ERR_NOT_FINISHED;
        }
        int got = esp_https_ota_get_image_len_read(h);
        int pct = plain_size > 0 ? (int)((int64_t)got * 100 / plain_size) : 0;
        if (pct != last_pct) { last_pct = pct; ota_ui(OTA_UI_DOWNLOAD, pct, NULL); }
    }
    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
        esp_https_ota_abort(h);
        return err != ESP_OK ? err : ESP_FAIL;
    }
    ota_ui(OTA_UI_VERIFY, 100, NULL);
    if (!sha_check(part, plain_size, sha_hex)) {
        esp_https_ota_abort(h);
        return ESP_ERR_INVALID_CRC;
    }
    return esp_https_ota_finish(h);
}

static void ota_task(void *arg)
{
    (void)arg;
    char ssid[33] = {0}, psk[65] = {0};
    char *manifest = NULL;
    cJSON *root = NULL;
    const char *fail = NULL;
    bool show_last = false;
    bool wifi_up = false, espnow_paused = false;

    ota_ui(OTA_UI_CHECKING, 0, NULL);

    espnow_wifi_req_t creds = espnow_client_request_wifi(ssid, sizeof(ssid), psk, sizeof(psk),
                                                         CREDS_TIMEOUT_MS);
    if (creds != ESPNOW_WIFI_REQ_OK) {
        fail = creds == ESPNOW_WIFI_REQ_NO_CREDS ? "Controller has no Wi-Fi" : "Controller unreachable";
        goto done;
    }
    if (s_cancel) goto done;

    espnow_client_ota_pause(true);
    espnow_paused = true;
    if (!wifi_sta_join(ssid, psk, &s_cancel)) {
        if (!s_cancel) fail = "Wi-Fi failed";
        goto done;
    }
    wifi_up = true;
    memset(psk, 0, sizeof(psk));

    {
        esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&sc);
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_TIMEOUT_MS));
        esp_netif_sntp_deinit();
    }
    if (s_cancel) goto done;

    manifest = fetch_manifest();
    if (!manifest) { fail = "Update check failed"; goto done; }
    root = cJSON_Parse(manifest);
    if (!root) { fail = "Bad manifest"; goto done; }

    {
        const cJSON *jver = cJSON_GetObjectItem(root, "version");
        const cJSON *builds = cJSON_GetObjectItem(root, "builds");
        const char *path = NULL, *sha = NULL;
        int size = 0;
        const cJSON *b;
        cJSON_ArrayForEach(b, builds) {
            const cJSON *board = cJSON_GetObjectItem(b, "board");
            if (cJSON_IsString(board) && strcmp(board->valuestring, LINK_BOARD_ID) == 0) {
                const cJSON *jp = cJSON_GetObjectItem(b, "path");
                const cJSON *js = cJSON_GetObjectItem(b, "sha256");
                const cJSON *jz = cJSON_GetObjectItem(b, "size");
                if (cJSON_IsString(jp)) path = jp->valuestring;
                if (cJSON_IsString(js)) sha = js->valuestring;
                if (cJSON_IsNumber(jz)) size = jz->valueint;
                break;
            }
        }
        if (!cJSON_IsString(jver) || !path || !sha || size <= 0) { fail = "Bad manifest"; goto done; }

        const char *local = esp_app_get_description()->version;
        if (ota_ver_cmp(jver->valuestring, local) <= 0) {
            ota_ui(OTA_UI_UPTODATE, 0, local);
            show_last = true;
            goto done;
        }

        ota_ui(OTA_UI_CONFIRM, 0, jver->valuestring);
        s_confirm = false;
        for (int i = 0; i < CONFIRM_TIMEOUT_MS / 100 && !s_confirm && !s_cancel; i++)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (!s_confirm || s_cancel) goto done;

        char fw_url[280], murl[OTA_URL_MAX];
        ota_manifest_url(murl, sizeof murl);
        const char *slash = strrchr(murl, '/');
        int baselen = slash ? (int)(slash - murl) : (int)strlen(murl);
        snprintf(fw_url, sizeof(fw_url), "%.*s/%s", baselen, murl, path);

        ota_ui(OTA_UI_DOWNLOAD, 0, NULL);
        esp_err_t err = download(fw_url, size, sha);
        if (err == ESP_ERR_NOT_FINISHED) goto done;
        if (err != ESP_OK) { fail = "Download failed"; goto done; }

        ota_ui(OTA_UI_OK, 100, NULL);
        ESP_LOGI(TAG, "update installed, restarting");
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }

done:
    memset(ssid, 0, sizeof(ssid));
    memset(psk, 0, sizeof(psk));
    if (root) cJSON_Delete(root);
    free(manifest);
    if (wifi_up) esp_wifi_disconnect();
    if (espnow_paused) espnow_client_ota_pause(false);
    if (fail)          ota_ui(OTA_UI_FAIL, 0, fail);
    else if (!show_last) ota_ui(OTA_UI_HIDDEN, 0, NULL);
    s_running = false;
}

void link_ota_begin(void)
{
    if (s_running) return;
    s_cancel = false; s_confirm = false;
    s_running = true;
    if (!espnow_client_worker_submit(ota_task, NULL)) {
        s_running = false;
        ESP_LOGE(TAG, "OTA submit refused (worker busy)");
        ota_ui(OTA_UI_FAIL, 0, "Link busy, try again");
    }
}

void link_ota_confirm(void) { s_confirm = true; }
void link_ota_cancel(void)  { s_cancel  = true; }
