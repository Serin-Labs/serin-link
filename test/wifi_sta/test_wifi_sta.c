/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "wifi_sta.h"

/* A simulated driver: the real wifi_sta.c registers its handler here, and the
 * driver calls back into it the way ESP-IDF's event loop would. */
esp_event_base_t const WIFI_EVENT = "WIFI_EVENT";
esp_event_base_t const IP_EVENT = "IP_EVENT";

static esp_event_handler_t wifi_handler, ip_handler;
static int registered, configured, connects, disconnects, delays;
static esp_err_t config_result, connect_result;
static int ip_on_connect;     /* the connect call that yields an IP; 0 = none */
static bool drop_each_delay;  /* deliver STA_DISCONNECTED on every delay */
static int cancel_at_delay;   /* set `cancel` on this delay; 0 = never */
static volatile bool cancel;
static wifi_config_t applied;

esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
                                              esp_event_handler_t handler, void *arg,
                                              esp_event_handler_instance_t *instance)
{
    (void)arg;
    if (base == WIFI_EVENT) { assert(id == ESP_EVENT_ANY_ID); wifi_handler = handler; }
    else { assert(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP); ip_handler = handler; }
    *instance = (void *)(uintptr_t)++registered;
    return ESP_OK;
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
                                                esp_event_handler_instance_t instance)
{
    (void)id; (void)instance;
    if (base == WIFI_EVENT) wifi_handler = NULL; else ip_handler = NULL;
    --registered;
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t iface, wifi_config_t *conf)
{
    assert(iface == WIFI_IF_STA);
    applied = *conf;
    ++configured;
    return config_result;
}

esp_err_t esp_wifi_connect(void)
{
    ++connects;
    if (connect_result == ESP_OK && connects == ip_on_connect)
        ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
    return connect_result;
}

esp_err_t esp_wifi_disconnect(void) { ++disconnects; return ESP_OK; }

void vTaskDelay(const TickType_t ticks)
{
    assert(ticks == 100);
    ++delays;
    if (drop_each_delay) wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
    if (delays == cancel_at_delay) cancel = true;
}

static void reset(void)
{
    memset(&applied, 0xa5, sizeof applied);
    registered = configured = connects = disconnects = delays = 0;
    config_result = connect_result = ESP_OK;
    ip_on_connect = 1;
    drop_each_delay = false;
    cancel_at_delay = 0;
    cancel = false;
}

static void expect_join(const char *ssid, const char *psk)
{
    reset();
    assert(wifi_sta_join(ssid, psk, NULL));
    assert(configured == 1 && connects == 1 && registered == 0 && disconnects == 0);
    size_t sl = strlen(ssid), pl = strlen(psk);
    assert(memcmp(applied.sta.ssid, ssid, sl) == 0);
    assert(memcmp(applied.sta.password, psk, pl) == 0);
    for (size_t i = sl; i < sizeof applied.sta.ssid; ++i) assert(applied.sta.ssid[i] == 0);
    for (size_t i = pl; i < sizeof applied.sta.password; ++i) assert(applied.sta.password[i] == 0);
}

static void expect_invalid(const char *ssid, const char *psk)
{
    reset();
    assert(!wifi_sta_join(ssid, psk, NULL));
    assert(configured == 0 && connects == 0 && registered == 0 && disconnects == 0);
}

static void test_credential_boundaries(void)
{
    char ssid[34], psk[66];
    // Full-width fields need all bytes, with no terminator inside the driver.
    memset(ssid, 's', 32); ssid[32] = 0;
    memset(psk, 'a', 64); psk[64] = 0;
    expect_join(ssid, psk);
    const size_t passwords[] = {0, 8, 63, 64};
    for (size_t sl = 1; sl <= 32; ++sl) {
        memset(ssid, 's', sl); ssid[sl] = 0;
        for (size_t p = 0; p < sizeof passwords / sizeof passwords[0]; ++p) {
            memset(psk, 'a', passwords[p]); psk[passwords[p]] = 0;
            expect_join(ssid, psk);
        }
    }
    // The limit is bytes, including UTF-8 sequences, not characters.
    for (size_t i = 0; i < 32; i += 2) { ssid[i] = (char)0xc3; ssid[i + 1] = (char)0xa9; }
    ssid[32] = 0;
    expect_join(ssid, "ordinary-passphrase");
    // Do not silently join a prefix of an invalid overlong network name/key.
    memset(ssid, 's', 33); ssid[33] = 0;
    expect_invalid(ssid, "ordinary-passphrase");
    memset(psk, 'a', 65); psk[65] = 0;
    expect_invalid("Home", psk);
    expect_invalid("", "ordinary-passphrase");
    expect_invalid(NULL, "ordinary-passphrase");
    expect_invalid("Home", NULL);
}

static void test_driver_failures(void)
{
    // A rejected config must not reconnect using the previous credentials.
    reset(); config_result = ESP_FAIL;
    assert(!wifi_sta_join("Home", "ordinary-passphrase", NULL));
    assert(configured == 1 && connects == 0 && delays == 0);
    assert(registered == 0 && disconnects == 1);
    reset(); connect_result = ESP_FAIL;
    assert(!wifi_sta_join("Home", "ordinary-passphrase", NULL));
    assert(connects == 1 && delays == 0 && registered == 0 && disconnects == 1);
}

static void test_timeout_and_cancel(void)
{
    reset(); ip_on_connect = 0;
    assert(!wifi_sta_join("Home", "ordinary-passphrase", NULL));
    assert(delays == 20000 / 100 && registered == 0 && disconnects == 1);
    reset(); ip_on_connect = 0; cancel_at_delay = 5;
    assert(!wifi_sta_join("Home", "ordinary-passphrase", &cancel));
    assert(delays == 5 && registered == 0 && disconnects == 1);
}

static void test_reconnects(void)
{
    // A dropped association is retried, and an IP on a retry is a join.
    reset(); ip_on_connect = 3; drop_each_delay = true;
    assert(wifi_sta_join("Home", "ordinary-passphrase", NULL));
    assert(connects == 3 && delays == 2 && disconnects == 0);
    // Retries stop after three; the join then times out.
    reset(); ip_on_connect = 0; drop_each_delay = true;
    assert(!wifi_sta_join("Home", "ordinary-passphrase", NULL));
    assert(connects == 1 + 3 && delays == 20000 / 100 && disconnects == 1);
    // An IP from an earlier join does not carry over into the next one.
    expect_join("Home", "ordinary-passphrase");
    ip_on_connect = 0; connects = 0;
    assert(!wifi_sta_join("Home", "ordinary-passphrase", NULL));
}

int main(void)
{
    test_credential_boundaries();
    test_driver_failures();
    test_timeout_and_cancel();
    test_reconnects();
    puts("wifi_sta: credential boundaries, driver failures, timeout, cancel, retries passed");
    return 0;
}
