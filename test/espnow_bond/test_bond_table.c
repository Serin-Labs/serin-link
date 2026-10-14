/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
/* Run the real bond table (main/espnow_bond.c) on stubbed NVS and FreeRTOS.
 * Every NVS access must happen under the table lock, every call must release
 * it, and a write keyed to a removed zone must not land on another zone. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nvs_flash.h>
#include "freertos/semphr.h"
#include "espnow_bond.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: ", __func__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); failures++; } } while (0)

/* Mutex stand-in. */
static StaticSemaphore_t *s_made;
static int lock_depth, lock_takes, lock_misuse;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf) {
    buf->taken = 0; s_made = buf; return buf;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h, TickType_t wait) {
    (void)wait;
    if (h != s_made || h->taken) lock_misuse++;   /* not recursive */
    h->taken = 1; lock_depth++; lock_takes++;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {
    if (h != s_made || !h->taken) lock_misuse++;
    h->taken = 0; lock_depth--;
    return pdTRUE;
}

/* NVS stand-in: one blob, every access checked against the lock. */
static uint8_t nvs_blob[ESPNOW_BONDS_BLOB_MAX];
static size_t  nvs_len;
static int     nvs_unlocked, nvs_writes;
static int     fail_writes;
static void nvs_touch(void) { if (lock_depth != 1) nvs_unlocked++; }
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out) {
    (void)ns; (void)mode; nvs_touch(); *out = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len) {
    (void)h; (void)key; nvs_touch();
    if (!nvs_len) return ESP_FAIL;
    if (*len < nvs_len) return ESP_FAIL;
    memcpy(out, nvs_blob, nvs_len); *len = nvs_len;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *val, size_t len) {
    (void)h; (void)key; nvs_touch();
    if (fail_writes) return ESP_FAIL;
    memcpy(nvs_blob, val, len); nvs_len = len; nvs_writes++;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; nvs_touch(); return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; nvs_touch(); }

static espnow_bond_rec_t mk(uint8_t tag) {
    espnow_bond_rec_t r; memset(&r, 0, sizeof r);
    memset(r.mac, tag, 6); memset(r.lmk, tag ^ 0xFF, 16);
    r.last_channel = 1;
    snprintf(r.name, sizeof r.name, "Zone %u", (unsigned)tag);
    return r;
}

static void after_call(const char *what) {
    CHECK(lock_depth == 0, "%s left the bond lock held", what);
    CHECK(lock_misuse == 0, "%s took the bond lock recursively or gave it unheld", what);
    CHECK(nvs_unlocked == 0, "%s touched NVS outside the bond lock", what);
}

static int stored(espnow_bond_rec_t out[ESPNOW_MAX_BONDS]) {
    return espnow_bonds_decode(nvs_blob, nvs_len, out);
}

static void test_lock_and_writes(void) {
    espnow_bond_rec_t tab[ESPNOW_MAX_BONDS];
    int takes = lock_takes;
    CHECK(espnow_bonds_load(tab) == 0, "fresh table not empty");
    after_call("espnow_bonds_load");
    CHECK(lock_takes > takes, "espnow_bonds_load did not take the bond lock");

    espnow_bond_rec_t a = mk(1), b = mk(2), c = mk(3);
    takes = lock_takes;
    CHECK(espnow_bond_add(&a) == 0 && espnow_bond_add(&b) == 1 && espnow_bond_add(&c) == 2,
          "adds did not append");
    after_call("espnow_bond_add");
    CHECK(lock_takes >= takes + 3, "espnow_bond_add did not take the bond lock");

    takes = lock_takes;
    espnow_bond_set_channel(b.mac, 6);
    after_call("espnow_bond_set_channel");
    CHECK(lock_takes > takes, "espnow_bond_set_channel did not take the bond lock");

    takes = lock_takes;
    espnow_bond_set_name(c.mac, "Den", 4);
    after_call("espnow_bond_set_name");
    CHECK(lock_takes > takes, "espnow_bond_set_name did not take the bond lock");

    espnow_bond_rec_t nv[ESPNOW_MAX_BONDS];
    CHECK(stored(nv) == 3, "NVS holds %d bonds, want 3", stored(nv));
    CHECK(nv[1].last_channel == 6, "channel not persisted");
    CHECK(strcmp(nv[2].name, "Den") == 0 && nv[2].caps_seq == 4, "name not persisted");

    /* Forget the first zone: the others move down one slot. Writes still
     * keyed to it must not land on the zone that took its slot. */
    takes = lock_takes;
    espnow_bond_remove(0);
    after_call("espnow_bond_remove");
    CHECK(lock_takes > takes, "espnow_bond_remove did not take the bond lock");
    int writes = nvs_writes;
    espnow_bond_set_channel(a.mac, 11);
    espnow_bond_set_name(a.mac, "Gone", 9);
    after_call("writes for a removed zone");
    CHECK(nvs_writes == writes, "a removed zone's write reached NVS");
    espnow_bond_set_channel(c.mac, 3);
    after_call("espnow_bond_set_channel");
    CHECK(stored(nv) == 2, "NVS holds %d bonds, want 2", stored(nv));
    CHECK(memcmp(nv[0].mac, b.mac, 6) == 0 && nv[0].last_channel == 6 &&
          strcmp(nv[0].name, "Zone 2") == 0, "zone B was overwritten");
    CHECK(memcmp(nv[1].mac, c.mac, 6) == 0 && nv[1].last_channel == 3 &&
          strcmp(nv[1].name, "Den") == 0, "zone C lost its update");
    CHECK(espnow_bonds_load(tab) == 2 && memcmp(tab, nv, 2 * sizeof tab[0]) == 0,
          "table and NVS disagree");
    after_call("espnow_bonds_load");
}

static void test_failure_paths_release(void) {
    espnow_bond_rec_t tab[ESPNOW_MAX_BONDS], before[ESPNOW_MAX_BONDS];
    int n = espnow_bonds_load(before);
    fail_writes = 1;
    espnow_bond_rec_t d = mk(4);
    CHECK(espnow_bond_add(&d) == -2, "failed write did not report -2");
    after_call("espnow_bond_add (write fails)");
    CHECK(espnow_bonds_load(tab) == n && memcmp(tab, before, sizeof tab) == 0,
          "failed write changed the table");
    fail_writes = 0;
    for (uint8_t t = 10; t < 30 && espnow_bonds_load(tab) < ESPNOW_MAX_BONDS; t++) {
        espnow_bond_rec_t r = mk(t);
        CHECK(espnow_bond_add(&r) >= 0, "add %u failed", (unsigned)t);
    }
    espnow_bond_rec_t extra = mk(99);
    CHECK(espnow_bond_add(&extra) == -1, "full table did not report -1");
    after_call("espnow_bond_add (table full)");
    espnow_bond_remove(ESPNOW_MAX_BONDS);
    after_call("espnow_bond_remove (out of range)");
}

int main(void) {
    test_lock_and_writes();
    test_failure_paths_release();
    if (failures) return 1;
    printf("test_bond_table: all tests passed\n");
    return 0;
}
