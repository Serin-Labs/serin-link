/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "sensor_sht40.h"
#if CONFIG_SERIN_SHT40

#include "sht40_convert.h"
#include "prefs.h"
#include "driver/i2c_master.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sensor_sht40";

#define SHT40_PORT        I2C_NUM_1
#define SHT40_HZ          100000
#define SHT40_CMD_MEASURE 0xFD
#define SHT40_CMD_SERIAL  0x89
#define SHT40_CMD_RESET   0x94
#define SHT40_MEAS_MS     10
#define SAMPLE_MS         10000
#define FAIL_BACKOFF_MS   (SAMPLE_MS * 2)
#define TEMP_FILTER_TAU_MS   60000
#define MAX_FAILS         3
#define PROBE_TRIES       3
#define PROBE_RETRY_MS    100
#define BUS_MUX_MS        1000
#define PREF_OFFSET_KEY   "sht_off_cc"
#define OFFSET_MIN_CC     (-2000)
#define OFFSET_MAX_CC       2000
#define SCAN_PASSES         3

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool     s_present;

static SemaphoreHandle_t s_bus_mux;
static volatile int16_t  s_base_cc;

static volatile bool     s_idle_off;

static volatile int32_t  s_filt_q8;

static volatile bool     s_valid;
static volatile int16_t  s_temp_cc;
static volatile uint16_t s_hum_cc;
static volatile uint32_t s_stamp_ms;

static esp_err_t sht40_cmd(uint8_t cmd)
{
    return i2c_master_transmit(s_dev, &cmd, 1, 100);
}

typedef enum {
    SAMPLE_OK,
    SAMPLE_FAIL,
    SAMPLE_SKIP,
} sample_res_t;

static sample_res_t sht40_measure(int16_t *cc, uint16_t *rh_cc)
{
    if (s_bus_mux && xSemaphoreTake(s_bus_mux, pdMS_TO_TICKS(BUS_MUX_MS)) != pdTRUE) {
        ESP_LOGD(TAG, "sample skipped — bus held (i2c-scan?)");
        return SAMPLE_SKIP;
    }
    bool ok = false;
    if (sht40_cmd(SHT40_CMD_MEASURE) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(SHT40_MEAS_MS));
        uint8_t rx[6];
        if (i2c_master_receive(s_dev, rx, sizeof rx, 100) == ESP_OK)
            ok = sht40_parse(rx, cc, rh_cc);
    }
    if (s_bus_mux) xSemaphoreGive(s_bus_mux);
    return ok ? SAMPLE_OK : SAMPLE_FAIL;
}

static void sht40_bus_recover(bool first_of_episode)
{
    if (s_bus_mux && xSemaphoreTake(s_bus_mux, pdMS_TO_TICKS(BUS_MUX_MS)) != pdTRUE) {
        if (first_of_episode)
            ESP_LOGW(TAG, "bus recovery deferred — bus held by a scan");
        return;
    }
    const esp_err_t err = i2c_master_bus_reset(s_bus);
    (void)sht40_cmd(SHT40_CMD_RESET);
    if (s_bus_mux) xSemaphoreGive(s_bus_mux);
    if (first_of_episode)
        ESP_LOGW(TAG, "bus recovery: clear+reset %s, sensor soft reset sent",
                 err == ESP_OK ? "ok" : esp_err_to_name(err));
}

static bool sht40_probe_once(void)
{
    static const uint8_t ADDRS[] = { 0x44, 0x45, 0x46 };
    for (unsigned i = 0; i < sizeof ADDRS; i++) {
        const i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = ADDRS[i],
            .scl_speed_hz    = SHT40_HZ,
        };
        if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) continue;

        (void)sht40_cmd(SHT40_CMD_RESET);
        vTaskDelay(pdMS_TO_TICKS(2));
        uint8_t rx[6];
        if (sht40_cmd(SHT40_CMD_SERIAL) == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(2));
            if (i2c_master_receive(s_dev, rx, sizeof rx, 100) == ESP_OK &&
                sht40_crc8(rx, 2) == rx[2] && sht40_crc8(rx + 3, 2) == rx[5]) {
                ESP_LOGI(TAG, "found at 0x%02X, serial %02X%02X%02X%02X",
                         ADDRS[i], rx[0], rx[1], rx[3], rx[4]);
                return true;
            }
        }
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    return false;
}

static bool sht40_probe(void)
{
    for (unsigned t = 0; t < PROBE_TRIES; t++) {
        if (t) {
            vTaskDelay(pdMS_TO_TICKS(PROBE_RETRY_MS));
            if (t + 1 == PROBE_TRIES) (void)i2c_master_bus_reset(s_bus);
        }
        if (sht40_probe_once()) {
            if (t) ESP_LOGW(TAG, "answered only on probe attempt %u of %d",
                            t + 1, PROBE_TRIES);
            return true;
        }
    }
    return false;
}

static void sht40_task(void *arg)
{
    (void)arg;
    int fails = 0;
    for (;;) {
        int16_t cc; uint16_t rh_cc;
        const sample_res_t got = sht40_measure(&cc, &rh_cc);
        if (got == SAMPLE_OK) {
            const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            s_filt_q8  = s_valid
                       ? sht40_lpf_step_q8(s_filt_q8, cc, now_ms - s_stamp_ms,
                                           TEMP_FILTER_TAU_MS)
                       : sht40_q8_from_cc(cc);
            s_temp_cc  = cc;
            s_hum_cc   = rh_cc;
            s_stamp_ms = now_ms;
            s_valid    = true;
            fails      = 0;
        } else if (got == SAMPLE_FAIL && ++fails >= MAX_FAILS) {
            const bool first_of_episode = (fails == MAX_FAILS);
            if (s_valid) ESP_LOGW(TAG, "%d consecutive bad samples — invalidating", fails);
            s_valid  = false;
            sht40_bus_recover(first_of_episode);
        }

        const uint32_t delay_ms = fails < MAX_FAILS ? SAMPLE_MS : FAIL_BACKOFF_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

bool sensor_sht40_present(void) { return s_present; }

static int16_t offset_eff(void)
{
    return sht40_offset_eff(s_base_cc, s_idle_off,
                            CONFIG_SERIN_SHT40_IDLE_OFF_DELTA_CC);
}

bool sensor_sht40_read(int16_t *temp_cc, uint16_t *hum_cc, uint32_t *age_ms)
{
    if (!s_present || !s_valid) return false;
    if (temp_cc) *temp_cc = (int16_t)(sht40_q8_to_cc(s_filt_q8) - offset_eff());
    if (hum_cc)  *hum_cc  = sht40_rh_apply_gain(s_hum_cc,
                                                CONFIG_SERIN_SHT40_RH_GAIN_PPT);
    if (age_ms) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        *age_ms = now_ms - s_stamp_ms;
    }
    return true;
}


int16_t sensor_sht40_offset_cc(void) { return offset_eff(); }

static void fmt_cc(char *buf, size_t n, int16_t cc)
{
    const int mag = abs(cc);
    snprintf(buf, n, "%s%d.%02d", cc < 0 ? "-" : "", mag / 100, mag % 100);
}

void sensor_sht40_set_base_offset_cc(int16_t cc)
{
    s_base_cc = cc;
    prefs_set_i16(PREF_OFFSET_KEY, cc);
    char base_s[8], eff_s[8];
    fmt_cc(base_s, sizeof base_s, cc);
    fmt_cc(eff_s, sizeof eff_s, offset_eff());
    ESP_LOGI(TAG, "base offset set to %s C (effective %s C, idle %s)",
             base_s, eff_s, s_idle_off ? "off" : "glance");
}

void sensor_sht40_set_idle_off(bool off)
{
    if (off == s_idle_off) return;
    s_idle_off = off;
    if (!s_present) return;
    char eff_s[8];
    fmt_cc(eff_s, sizeof eff_s, offset_eff());
    ESP_LOGI(TAG, "idle screen %s -> offset %s C", off ? "off" : "glance", eff_s);
}

static int cmd_sensor(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_present) { printf("sht4x: not present\n"); return 0; }
    if (!s_valid)   { printf("sht4x: present, no valid reading\n"); return 0; }
    const int16_t raw  = s_temp_cc;
    const int16_t filt = sht40_q8_to_cc(s_filt_q8);
    const int16_t off  = offset_eff();
    const int16_t cor  = (int16_t)(filt - off);
    const uint32_t age_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_stamp_ms;
    char raw_s[8], filt_s[8], base_s[8], off_s[8], cor_s[8], rh_s[8], mode[24];
    fmt_cc(raw_s,  sizeof raw_s,  raw);
    fmt_cc(filt_s, sizeof filt_s, filt);
    fmt_cc(base_s, sizeof base_s, s_base_cc);
    fmt_cc(off_s,  sizeof off_s,  off);
    fmt_cc(cor_s,  sizeof cor_s,  cor);
    fmt_cc(rh_s,   sizeof rh_s,   (int16_t)s_hum_cc);
    if (s_idle_off) {
        char d_s[8];
        fmt_cc(d_s, sizeof d_s, CONFIG_SERIN_SHT40_IDLE_OFF_DELTA_CC);
        snprintf(mode, sizeof mode, "idle=off -%s", d_s);
    } else {
        snprintf(mode, sizeof mode, "idle=glance");
    }
    printf("sht4x: raw %s C  filt %s C (tau %ds)  offset %s (base %s, %s)  corrected %s C  rh %s%%  age %lums\n",
           raw_s, filt_s, TEMP_FILTER_TAU_MS / 1000, off_s, base_s, mode,
           cor_s, rh_s, (unsigned long)age_ms);
    return 0;
}

static bool parse_cc_arg(const char *cmd, int argc, char **argv,
                         long min, long max, long *out)
{
    if (argc != 2) { printf("usage: %s <centi-C>\n", cmd); return false; }
    char *end = NULL;
    const long v = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0') {
        printf("usage: %s <centi-C> (not a number: \"%s\")\n", cmd, argv[1]);
        return false;
    }
    if (v < min || v > max) {
        printf("usage: %s <centi-C>, range %ld..%ld\n", cmd, min, max);
        return false;
    }
    *out = v;
    return true;
}

static int cmd_sensor_offset(int argc, char **argv)
{
    long v;
    if (!parse_cc_arg("sensor-offset", argc, argv, OFFSET_MIN_CC, OFFSET_MAX_CC, &v))
        return 1;
    sensor_sht40_set_base_offset_cc((int16_t)v);
    return 0;
}

static int cmd_i2c_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (!s_bus) { printf("i2c: bus not up\n"); return 1; }
    if (s_bus_mux && xSemaphoreTake(s_bus_mux, pdMS_TO_TICKS(3000)) != pdTRUE) {
        printf("i2c-scan: bus busy\n");
        return 1;
    }
    uint8_t seen[0x78 - 0x08] = {0};
    for (int pass = 0; pass < SCAN_PASSES; pass++)
        for (uint8_t a = 0x08; a < 0x78; a++) {
            if (i2c_master_probe(s_bus, a, 50) == ESP_OK) seen[a - 0x08]++;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    if (s_bus_mux) xSemaphoreGive(s_bus_mux);

    int found = 0, flaky = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (seen[a - 0x08] == SCAN_PASSES) { printf("  0x%02X\n", a); found++; }
        else if (seen[a - 0x08] > 0)       flaky++;
    }
    printf("i2c-scan: %d device(s) on I2C_NUM_%d", found, SHT40_PORT);
    if (flaky) printf(" (%d unstable address(es) ignored)", flaky);
    printf("\n");
    return 0;
}

static void register_console(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "sensor", .help = "SHT4x reading (raw, offset, corrected)",
          .func = cmd_sensor },
        { .command = "sensor-offset", .help = "Set SHT4x base self-heating offset (centi-C)",
          .func = cmd_sensor_offset },
        { .command = "i2c-scan", .help = "Scan the sensor I2C bus", .func = cmd_i2c_scan },
    };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++)
        esp_console_cmd_register(&cmds[i]);
}

bool sensor_sht40_init(void)
{
    s_base_cc = prefs_get_i16(PREF_OFFSET_KEY, INT16_MIN);
    if (s_base_cc == INT16_MIN) {
        const int16_t legacy_dc = prefs_get_i16("sht_off", INT16_MIN);
        if (legacy_dc != INT16_MIN) {
            s_base_cc = sht40_offset_dc_to_cc(legacy_dc);
            prefs_set_i16(PREF_OFFSET_KEY, s_base_cc);
            prefs_erase("sht_off");
            ESP_LOGI(TAG, "migrated offset %d deci-C -> %d centi-C",
                     legacy_dc, s_base_cc);
        } else {
            s_base_cc = CONFIG_SERIN_SHT40_OFFSET_CC;
        }
    }
    s_bus_mux   = xSemaphoreCreateMutex();
    register_console();

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = SHT40_PORT,
        .sda_io_num        = CONFIG_SERIN_SHT40_SDA_GPIO,
        .scl_io_num        = CONFIG_SERIN_SHT40_SCL_GPIO,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGW(TAG, "I2C_NUM_%d bring-up failed — sensor disabled", SHT40_PORT);
        return false;
    }

    s_present = sht40_probe();
    if (!s_present) {
        ESP_LOGI(TAG, "no sensor on SDA=%d SCL=%d (carrier absent?)",
                 CONFIG_SERIN_SHT40_SDA_GPIO, CONFIG_SERIN_SHT40_SCL_GPIO);
        return false;
    }

    xTaskCreate(sht40_task, "sht40", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "sampling every %d ms, filter tau %d s, offset %d centi-C (base %d, idle %s)",
             SAMPLE_MS, TEMP_FILTER_TAU_MS / 1000, offset_eff(), s_base_cc,
             s_idle_off ? "off" : "glance");
    return true;
}

#endif
