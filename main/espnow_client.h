/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void espnow_client_start(void);
void espnow_client_boot_ui(void);
void espnow_client_on_local_edit(void);
void espnow_client_on_units_edit(bool use_f);
void espnow_client_on_room_source_id(uint64_t source_id);
void espnow_client_begin_pairing(void);
void espnow_client_cancel_pairing(void);
void espnow_client_forget(void);
typedef enum {
    ESPNOW_WIFI_REQ_OK,
    ESPNOW_WIFI_REQ_NO_REPLY,
    ESPNOW_WIFI_REQ_NO_CREDS,
} espnow_wifi_req_t;
espnow_wifi_req_t espnow_client_request_wifi(char *ssid, size_t ssid_len,
                                             char *psk, size_t psk_len, uint32_t timeout_ms);
void espnow_client_ota_pause(bool paused);

void espnow_client_radio_ps_override(bool enable, bool min);
bool espnow_client_radio_off(bool off);

bool espnow_client_worker_submit(void (*fn)(void *), void *arg);
void espnow_client_wifi_setup(void);
void espnow_client_wifi_setup_session(uint32_t session);
void espnow_client_wifi_cancel(uint32_t session);

bool espnow_client_link_alive(void);

void    espnow_client_self_mac(uint8_t out[6]);
uint8_t espnow_client_peer_version(void);
int     espnow_client_last_rx_age_s(void);

void espnow_client_set_active(int idx);
void espnow_client_refresh_idents(void);
