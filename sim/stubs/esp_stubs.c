// SPDX-License-Identifier: GPL-3.0-only
/* esp_stubs.c — PC implementations of the ESP APIs ui.c touches. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "prefs.h"
#include "factory_reset.h"
#include "sim.h"
#include "sim_time.h"

int64_t esp_timer_get_time(void) { return (int64_t)sim_time_ms() * 1000; }
esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_POWERON; }

/* espnow_client About-page getters (ui.c reads these three only). */
static uint8_t s_pv  = 10;   /* peer proto version; 10 = current, wifi wizard available */
static int     s_age = 2;    /* seconds since last STATE */
void sim_link_set(int pv, int age_s) { s_pv = (uint8_t)pv; s_age = age_s; }
void espnow_client_self_mac(uint8_t out[6])
{
    static const uint8_t m[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 }; /* synthetic local address */
    memcpy(out, m, 6);
}
uint8_t espnow_client_peer_version(void)  { return s_pv; }
int     espnow_client_last_rx_age_s(void) { return s_age; }

/* prefs: one "key=0|1" line per entry in ./.prefs (persists wifi_skip etc.). */
#define PREFS_FILE ".prefs"
#define PREFS_MAX  16
bool prefs_get_bool(const char *key, bool def)
{
    FILE *f = fopen(PREFS_FILE, "r");
    if (!f) return def;
    char k[64]; int v; bool r = def;
    while (fscanf(f, "%63[^=]=%d\n", k, &v) == 2) if (!strcmp(k, key)) r = v;
    fclose(f);
    return r;
}
void prefs_set_bool(const char *key, bool val)
{
    char keys[PREFS_MAX][64]; int vals[PREFS_MAX]; int n = 0;
    FILE *f = fopen(PREFS_FILE, "r");
    if (f) {
        while (n < PREFS_MAX && fscanf(f, "%63[^=]=%d\n", keys[n], &vals[n]) == 2)
            if (strcmp(keys[n], key)) n++;
        fclose(f);
    }
    f = fopen(PREFS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s=%d\n", keys[i], vals[i]);
    fprintf(f, "%s=%d\n", key, val ? 1 : 0);
    fclose(f);
}

/* Same one-line-per-key ./.prefs file, u8-valued (idle_off etc.) — not a bool
 * reuse because prefs.h's real NVS-backed pair genuinely has two widths. */
uint8_t prefs_get_u8(const char *key, uint8_t def)
{
    FILE *f = fopen(PREFS_FILE, "r");
    if (!f) return def;
    char k[64]; int v; uint8_t r = def;
    while (fscanf(f, "%63[^=]=%d\n", k, &v) == 2) if (!strcmp(k, key)) r = (uint8_t)v;
    fclose(f);
    return r;
}
void prefs_set_u8(const char *key, uint8_t val)
{
    char keys[PREFS_MAX][64]; int vals[PREFS_MAX]; int n = 0;
    FILE *f = fopen(PREFS_FILE, "r");
    if (f) {
        while (n < PREFS_MAX && fscanf(f, "%63[^=]=%d\n", keys[n], &vals[n]) == 2)
            if (strcmp(keys[n], key)) n++;
        fclose(f);
    }
    f = fopen(PREFS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s=%d\n", keys[i], vals[i]);
    fprintf(f, "%s=%d\n", key, (int)val);
    fclose(f);
}

/* Same one-line-per-key ./.prefs file: drop the key's line (absent = no-op). */
void prefs_erase(const char *key)
{
    char keys[PREFS_MAX][64]; int vals[PREFS_MAX]; int n = 0;
    FILE *f = fopen(PREFS_FILE, "r");
    if (!f) return;
    while (n < PREFS_MAX && fscanf(f, "%63[^=]=%d\n", keys[n], &vals[n]) == 2)
        if (strcmp(keys[n], key)) n++;
    fclose(f);
    f = fopen(PREFS_FILE, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s=%d\n", keys[i], vals[i]);
    fclose(f);
}

/* main/factory_reset.c contributes the countdown UI; its hardware reset body
 * is excluded under SERIN_SIM. Latch the request here instead of erasing NVS
 * or restarting, so info can report reset_fired for interaction assertions. */
static bool s_reset_fired = false;
void factory_reset_execute(void) { s_reset_fired = true; }
bool sim_reset_fired(void) { return s_reset_fired; }
