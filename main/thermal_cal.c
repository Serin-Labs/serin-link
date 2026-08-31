/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "thermal_cal.h"
#if CONFIG_SERIN_THERMAL_CAL

#include "bsp.h"
#include "espnow_client.h"
#include "sensor_sht40.h"
#include "ui.h"
#include "driver/temperature_sensor.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "cal";

#define SAMPLE_MS 10000

#define LVGL_LOCK_MS 2000

#define F_PANEL_ASLEEP 0x01
#define F_WIFI_ASSOC   0x02
#define F_HTTP_LOAD    0x04
#define F_DEEP_OFF     0x08
#define F_RADIO_OFF    0x10
#define F_RADIO_FULL   0x20

typedef struct __attribute__((packed)) {
    uint32_t t_s;
    uint16_t step;
    uint8_t  regime;
    uint8_t  phase;
    int16_t  sht_raw_cc;
    uint16_t sht_rh_cc;
    uint8_t  sht_valid;
    int16_t  die_dc;
    int16_t  near_dc;
    uint8_t  near_rh;
    uint8_t  near_batt;
    int8_t   near_rssi;
    uint16_t near_age_s;
    int16_t  far_dc;
    uint8_t  far_rh;
    uint8_t  far_batt;
    int8_t   far_rssi;
    uint16_t far_age_s;
    uint8_t  bright_pct;
    uint8_t  flags;
    uint8_t  face;
    uint8_t  input_evts;
} cal_rec_t;
_Static_assert(sizeof(cal_rec_t) == 33, "cal_rec_t must stay 33 bytes");

#define CSV_HEADER \
    "t_s,step,regime,phase,sht_raw_cc,sht_rh_cc,sht_valid,die_dc," \
    "near_dc,near_rh,near_batt,near_rssi,near_age_s," \
    "far_dc,far_rh,far_batt,far_rssi,far_age_s," \
    "bright_pct,flags,face,input_evts"

typedef struct {
    const char   *name;
    uint8_t       bright_pct;
    bool          panel_asleep;
    ui_cal_face_t face;
    bool          wifi;
    bool          http;
    bool          deep_off;
    bool          radio_off;
    bool          radio_full;
} cal_regime_t;

#define REGIME_BASELINE 255
static const cal_regime_t BASELINE = {
    "baseline", 0, true, UI_CAL_LIVE, false, false, false, false, false
};
static const cal_regime_t REGIMES[] = {
  { "glance15",   15, false, UI_CAL_GLANCE,    false, false, false, false, false },
  { "live15",     15, false, UI_CAL_LIVE,      false, false, false, false, false },
  { "live50",     50, false, UI_CAL_LIVE,      false, false, false, false, false },
  { "live100",   100, false, UI_CAL_LIVE,      false, false, false, false, false },
  { "ring100",   100, false, UI_CAL_CN105_OFF, false, false, false, false, false },
  { "ring15",     15, false, UI_CAL_CN105_OFF, false, false, false, false, false },
  { "wifi15",     15, false, UI_CAL_LIVE,      true,  false, false, false, false },
  { "trueoff",     0, true,  UI_CAL_LIVE,      false, false, true,  false, false },
  { "http15",     15, false, UI_CAL_LIVE,      true,  true,  false, false, false },
  { "radiooff",    0, true,  UI_CAL_LIVE,      false, false, false, true,  false },
  { "radiofull",   0, true,  UI_CAL_LIVE,      false, false, false, false, true  },
};
#define N_REGIMES ((int)(sizeof REGIMES / sizeof REGIMES[0]))

static const uint8_t SHORT_IDX[] = { 0, 4, 7, 3, 9, 10 };
#define N_SHORT ((int)(sizeof SHORT_IDX / sizeof SHORT_IDX[0]))

static cal_rec_t *s_ring;
static uint32_t   s_head;
static uint32_t   s_count;
static bool       s_wrapped;
static bool       s_owned;
static bool       s_deep_off;

static volatile uint8_t s_input_evts;

static temperature_sensor_handle_t s_die;
static bool                        s_die_ok;

static uint16_t s_step;
static uint8_t  s_regime_id;
static uint8_t  s_phase;
static uint8_t  s_bright;
static uint8_t  s_flags;
static uint8_t  s_face;

void thermal_cal_note_input(void)
{
    if (s_input_evts < 255) s_input_evts++;
}

bool thermal_cal_active(void) { return s_owned; }

static void fill_ref_absent(cal_rec_t *r)
{
    r->near_dc = r->far_dc = 0;
    r->near_rh = r->far_rh = 0;
    r->near_batt = r->far_batt = 0;
    r->near_rssi = r->far_rssi = 0;
    r->near_age_s = r->far_age_s = UINT16_MAX;
}

static void sample(cal_rec_t *r)
{
    memset(r, 0, sizeof *r);
    r->t_s    = (uint32_t)(esp_timer_get_time() / 1000000);
    r->step   = s_step;
    r->regime = s_regime_id;
    r->phase  = s_phase;

    int16_t t_cc = 0; uint16_t rh_cc = 0;
    r->sht_valid  = sensor_sht40_read_raw(&t_cc, &rh_cc, NULL) ? 1 : 0;
    r->sht_raw_cc = t_cc;
    r->sht_rh_cc  = rh_cc;

    float die_c = 0.0f;
    r->die_dc = (s_die_ok &&
                 temperature_sensor_get_celsius(s_die, &die_c) == ESP_OK)
                ? (int16_t)(die_c * 10.0f) : INT16_MIN;

    fill_ref_absent(r);

    r->bright_pct = s_bright;
    r->flags      = s_flags;
    r->face       = s_face;
    r->input_evts = s_input_evts;
    s_input_evts  = 0;
}

static void emit(const cal_rec_t *r)
{
    printf("%lu,%u,%u,%u,%d,%u,%u,%d,%d,%u,%u,%d,%u,%d,%u,%u,%d,%u,%u,%u,%u,%u\n",
           (unsigned long)r->t_s, r->step, r->regime, r->phase,
           r->sht_raw_cc, r->sht_rh_cc, r->sht_valid, r->die_dc,
           r->near_dc, r->near_rh, r->near_batt, r->near_rssi, r->near_age_s,
           r->far_dc, r->far_rh, r->far_batt, r->far_rssi, r->far_age_s,
           r->bright_pct, r->flags, r->face, r->input_evts);
}

static void ring_put(const cal_rec_t *r)
{
    if (!s_ring) return;
    s_ring[s_head] = *r;
    s_head = (s_head + 1) % CONFIG_SERIN_CAL_RING_RECS;
    if (s_count < CONFIG_SERIN_CAL_RING_RECS) {
        s_count++;
    } else if (!s_wrapped) {
        s_wrapped = true;
        ESP_LOGE(TAG, "RING WRAPPED — earliest records lost, raise "
                      "CONFIG_SERIN_CAL_RING_RECS or shorten the run");
    }
}

void thermal_cal_dump(void)
{
    if (!s_ring) { printf("# no ring\n"); return; }
    printf("# thermal-cal dump: %lu records%s\n",
           (unsigned long)s_count, s_wrapped ? " (WRAPPED — early data lost)" : "");
    printf("%s\n", CSV_HEADER);
    const uint32_t start = (s_count == CONFIG_SERIN_CAL_RING_RECS) ? s_head : 0;
    for (uint32_t i = 0; i < s_count; i++)
        emit(&s_ring[(start + i) % CONFIG_SERIN_CAL_RING_RECS]);
    printf("# dump end\n");
}

#define JOIN_TIMEOUT_MS 20000
#define LOAD_BUF        4096

static volatile bool s_got_ip;
static volatile bool s_load_run;
static volatile bool s_load_alive;
static bool          s_wifi_up;
static bool          s_radio_off;
static int           s_disc;

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (++s_disc <= 3 && !s_got_ip) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
    }
}

static bool cal_wifi_join(const char *ssid, const char *psk)
{
    esp_event_handler_instance_t h1 = NULL, h2 = NULL;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, &h1);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, &h2);
    s_got_ip = false; s_disc = 0;
    wifi_config_t wc; memset(&wc, 0, sizeof wc);
    snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", psk);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_connect();
    for (int i = 0; i < JOIN_TIMEOUT_MS / 100 && !s_got_ip; i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h1);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h2);
    if (!s_got_ip) esp_wifi_disconnect();
    return s_got_ip;
}

static void load_task(void *arg)
{
    (void)arg;
    char *buf = malloc(LOAD_BUF);
    if (!buf) {
        ESP_LOGE(TAG, "load buffer alloc failed");
        s_load_alive = false;
        vTaskDelete(NULL);
    }
    while (s_load_run) {
        esp_http_client_config_t hc = {
            .url               = CONFIG_SERIN_CAL_LOAD_URL,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms        = 30000,
            .keep_alive_enable = true,
        };
        esp_http_client_handle_t cl = esp_http_client_init(&hc);
        if (cl && esp_http_client_open(cl, 0) == ESP_OK) {
            esp_http_client_fetch_headers(cl);
            int r;
            while (s_load_run && (r = esp_http_client_read(cl, buf, LOAD_BUF)) > 0)
                ;
        }
        if (cl) esp_http_client_cleanup(cl);
        if (s_load_run) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    free(buf);
    s_load_alive = false;
    vTaskDelete(NULL);
}

static uint8_t radio_apply(const cal_regime_t *r)
{
    const bool want_wifi = r->wifi && CONFIG_SERIN_CAL_WIFI_SSID[0] != '\0';
    uint8_t flags = 0;

    if (!r->http && s_load_run) {
        s_load_run = false;
        for (int i = 0; i < 700 && s_load_alive; i++)
            vTaskDelay(pdMS_TO_TICKS(50));
        if (s_load_alive)
            ESP_LOGW(TAG, "load generator still running after 35 s");
    }

    if (want_wifi && !s_wifi_up) {
        espnow_client_ota_pause(true);
        if (cal_wifi_join(CONFIG_SERIN_CAL_WIFI_SSID, CONFIG_SERIN_CAL_WIFI_PSK)) {
            s_wifi_up = true;
        } else {
            espnow_client_ota_pause(false);
            ESP_LOGW(TAG, "wifi join failed — regime %s runs without it", r->name);
        }
    } else if (!r->wifi && s_wifi_up) {
        esp_wifi_disconnect();
        espnow_client_ota_pause(false);
        s_wifi_up = false;
    }
    if (s_wifi_up) flags |= F_WIFI_ASSOC;

    if (r->http && s_wifi_up && !s_load_run) {
        s_load_run   = true;
        s_load_alive = true;
        if (xTaskCreate(load_task, "cal_load", 6144, NULL, 3, NULL) != pdPASS) {
            s_load_run   = false;
            s_load_alive = false;
            ESP_LOGW(TAG, "load task create failed");
        }
    }
    if (s_load_run) flags |= F_HTTP_LOAD;

    if (r->radio_off != s_radio_off) {
        if (espnow_client_radio_off(r->radio_off)) s_radio_off = r->radio_off;
        else ESP_LOGW(TAG, "radio %s failed — regime %s runs without it",
                      r->radio_off ? "off" : "on", r->name);
    }
    if (s_radio_off) flags |= F_RADIO_OFF;
    espnow_client_radio_ps_override(r->radio_full, false);
    if (r->radio_full && !s_radio_off) flags |= F_RADIO_FULL;

    if (r->wifi && !want_wifi)
        ESP_LOGW(TAG, "regime %s wants Wi-Fi but CONFIG_SERIN_CAL_WIFI_SSID is "
                      "empty — running it without radio load", r->name);
    return flags;
}

static void apply_regime(const cal_regime_t *r, uint8_t regime_id, uint8_t phase)
{
    s_regime_id = regime_id;
    s_phase     = phase;
    s_bright    = r->panel_asleep ? 0 : r->bright_pct;
    s_face      = (uint8_t)r->face;
    s_flags     = (uint8_t)((r->panel_asleep ? F_PANEL_ASLEEP : 0) |
                            (r->deep_off     ? F_DEEP_OFF     : 0) |
                            radio_apply(r));

    if (lvgl_port_lock(LVGL_LOCK_MS)) {
        ui_cal_face(r->face);
        if (!r->deep_off && s_deep_off) { bsp_display_deep_off(false); s_deep_off = false; }
        if (r->panel_asleep) {
            bsp_display_sleep(true);
        } else {
            bsp_display_sleep(false);
            bsp_backlight_set(r->bright_pct);
        }
        if (r->deep_off && !s_deep_off) { bsp_display_deep_off(true); s_deep_off = true; }
        lvgl_port_unlock();
    } else {
        ESP_LOGW(TAG, "lvgl lock failed — regime %s not applied", r->name);
    }

    ESP_LOGI(TAG, "step %u %s regime=%s bright=%u%% panel=%s flags=0x%02X",
             s_step, phase ? "REGIME" : "base", r->name, s_bright,
             r->deep_off ? "DEEP-OFF" : (r->panel_asleep ? "dark" : "lit"),
             s_flags);
}

static void hold(int minutes)
{
    const int n = (minutes * 60 * 1000) / SAMPLE_MS;
    for (int i = 0; i < n; i++) {
        cal_rec_t r;
        sample(&r);
        ring_put(&r);
        emit(&r);
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

#if CONFIG_SERIN_CAL_DISPLAY_TEST
#define DT_HOLD_MS 15000

typedef struct { const char *what; int bright; bool sleep; bool deep; } dt_step_t;
static const dt_step_t DT[] = {
    { "100%% - should be BRIGHTEST",                100, false, false },
    { "50%%  - visibly dimmer than 100%%",           50, false, false },
    { "15%%  - dim, this is what Glance looks like", 15, false, false },
    { "0%%   - DARK (production 'Idle screen = Off')", 0, true,  false },
    { "SLPIN - DARK, panel genuinely asleep",         0, true,  true  },
    { "100%% AGAIN - if this is dark, SLPOUT FAILED",100, false, false },
};

static void display_test(void)
{
    ESP_LOGW(TAG, "DISPLAY CONTROL TEST — watch the dial. %d s per state.",
             DT_HOLD_MS / 1000);
    bool deep = false;
    for (unsigned pass = 1; ; pass++) {
        for (unsigned i = 0; i < sizeof DT / sizeof DT[0]; i++) {
            const dt_step_t *d = &DT[i];
            ESP_LOGW(TAG, "[pass %u  %u/%u]  %s", pass, i + 1,
                     (unsigned)(sizeof DT / sizeof DT[0]), d->what);
            if (lvgl_port_lock(LVGL_LOCK_MS)) {
                ui_cal_face(UI_CAL_LIVE);
                if (deep && !d->deep) { bsp_display_deep_off(false); deep = false; }
                bsp_display_sleep(d->sleep);
                if (!d->sleep) bsp_backlight_set(d->bright);
                if (d->deep && !deep) { bsp_display_deep_off(true); deep = true; }
                lvgl_port_unlock();
            } else {
                ESP_LOGE(TAG, "LVGL lock timeout — state NOT applied");
            }
            for (int t = 0; t < DT_HOLD_MS / 5000; t++) {
                vTaskDelay(pdMS_TO_TICKS(5000));
                int16_t raw = 0; uint16_t rh = 0;
                float die = 0.0f;
                sensor_sht40_read_raw(&raw, &rh, NULL);
                if (s_die_ok) temperature_sensor_get_celsius(s_die, &die);
                ESP_LOGI(TAG, "    sht_raw=%d.%02d C  die=%.1f C",
                         raw / 100, abs(raw % 100), die);
            }
        }
    }
}
#endif

static void sweep_task(void *arg)
{
    (void)arg;
#if CONFIG_SERIN_CAL_DISPLAY_TEST
    display_test();
#endif

#if CONFIG_SERIN_CAL_PROFILE_SHORT
    const bool short_profile = true;
#else
    const bool short_profile = false;
#endif
    const int n = short_profile ? N_SHORT : N_REGIMES;

    printf("# thermal-cal live stream — profile=%s regimes=%d "
           "settle=%dmin dwell=%dmin baseline=%dmin\n",
           short_profile ? "SHORT" : "FULL", n,
           CONFIG_SERIN_CAL_SETTLE_MIN, CONFIG_SERIN_CAL_DWELL_MIN,
           CONFIG_SERIN_CAL_BASELINE_MIN);
    printf("# sht_base_offset_cc=%d (NOT applied to sht_raw_cc)\n",
           sensor_sht40_offset_cc());
    printf("%s\n", CSV_HEADER);

    if (lvgl_port_lock(LVGL_LOCK_MS)) {
        ui_cal_take(true);
        lvgl_port_unlock();
        s_owned = true;
    } else {
        ESP_LOGE(TAG, "LVGL lock timeout — UI not running; regimes will NOT "
                      "actuate and the display columns are meaningless");
    }

    s_step = 0;
    apply_regime(&BASELINE, REGIME_BASELINE, 0);
    hold(CONFIG_SERIN_CAL_SETTLE_MIN);

    for (int i = 0; i < n; i++) {
        const uint8_t idx = short_profile ? SHORT_IDX[i] : (uint8_t)i;
        s_step = (uint16_t)(i + 1);
        apply_regime(&REGIMES[idx], idx, 1);
        hold(CONFIG_SERIN_CAL_DWELL_MIN);
        apply_regime(&BASELINE, REGIME_BASELINE, 0);
        hold(CONFIG_SERIN_CAL_BASELINE_MIN);
    }

    ESP_LOGW(TAG, "sweep complete — dumping");
    thermal_cal_dump();
    ESP_LOGW(TAG, "sweep complete — parked in baseline");
    for (;;) {
        cal_rec_t r;
        sample(&r);
        ring_put(&r);
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

void thermal_cal_start(void)
{
    ESP_LOGW(TAG, "THERMAL CAL BUILD — bench image, do not ship");

    s_ring = heap_caps_malloc((size_t)CONFIG_SERIN_CAL_RING_RECS * sizeof(cal_rec_t),
                              MALLOC_CAP_SPIRAM);
    if (!s_ring) {
        ESP_LOGE(TAG, "ring alloc failed (%d recs) — live stream only, a dropout "
                      "will lose the run", CONFIG_SERIN_CAL_RING_RECS);
    }

    const temperature_sensor_config_t tc = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&tc, &s_die) == ESP_OK &&
        temperature_sensor_enable(s_die) == ESP_OK) {
        s_die_ok = true;
    } else {
        ESP_LOGW(TAG, "die temperature sensor unavailable — die_dc column dead");
    }

    xTaskCreate(sweep_task, "cal", 4096, NULL, 3, NULL);
}

#endif
