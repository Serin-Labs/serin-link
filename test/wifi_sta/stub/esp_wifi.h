/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
/* Host stand-in: only the fields and calls main/wifi_sta.c uses, with the
 * real fixed-width STA credential fields. */
#pragma once
#include <stdint.h>
#include "esp_event.h"

typedef enum { WIFI_IF_STA = 0 } wifi_interface_t;
typedef union {
    struct { uint8_t ssid[32]; uint8_t password[64]; } sta;
} wifi_config_t;

enum { WIFI_EVENT_STA_DISCONNECTED = 5 };
enum { IP_EVENT_STA_GOT_IP = 0 };
extern esp_event_base_t const WIFI_EVENT;
extern esp_event_base_t const IP_EVENT;

esp_err_t esp_wifi_set_config(wifi_interface_t iface, wifi_config_t *conf);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
