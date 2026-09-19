/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "wifi_sta.h"
#include <string.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define JOIN_TIMEOUT_MS 20000

static volatile bool s_got_ip;
static volatile int  s_disc;

static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (++s_disc <= 3 && !s_got_ip) esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_got_ip = true;
    }
}

bool wifi_sta_join(const char *ssid, const char *psk, const volatile bool *cancel)
{
    wifi_config_t wc = {0};
    if (!ssid || !psk) return false;
    size_t ssid_len = strnlen(ssid, sizeof wc.sta.ssid + 1);
    size_t psk_len = strnlen(psk, sizeof wc.sta.password + 1);
    if (!ssid_len || ssid_len > sizeof wc.sta.ssid || psk_len > sizeof wc.sta.password)
        return false;
    /* STA fields are fixed-width bytes: maximum-length credentials use the
       whole field without a NUL. Shorter values keep the zero-filled tail. */
    memcpy(wc.sta.ssid, ssid, ssid_len);
    memcpy(wc.sta.password, psk, psk_len);
    esp_event_handler_instance_t h1 = NULL, h2 = NULL;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, &h1);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, &h2);
    s_got_ip = false; s_disc = 0;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err == ESP_OK) err = esp_wifi_connect();
    for (int i = 0; err == ESP_OK && i < JOIN_TIMEOUT_MS / 100 && !s_got_ip
                    && !(cancel && *cancel); i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h1);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h2);
    bool joined = err == ESP_OK && s_got_ip;
    if (!joined) esp_wifi_disconnect();
    return joined;
}
