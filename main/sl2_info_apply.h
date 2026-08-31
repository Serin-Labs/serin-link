/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stddef.h>
#include <string.h>
#include "ui.h"
#include "sl2_proto.h"
#include "sl2_ui_map.h"

static inline void sl2_info_apply(hvac_state_t *s, const uint8_t *tlvs, size_t len,
                                  bool use_f, uint8_t ftab_id)
{
    s->outside_valid = false;
    s->runtime_valid = false;
    s->sensor_batt_pct = -1;
    s->room_src        = SL2_ROOMSRC_INTERNAL;
    s->room_src_status = SL2_ROOMST_OK;

    size_t off = 0;
    uint8_t t, l;
    const uint8_t *v;
    while (sl2_tlv_next(tlvs, len, &off, &t, &l, &v)) {
        switch (t) {
        case SL2_TLV_WIFI_INFO:
            if (l >= 2) {
                s->wifi_rssi   = (int8_t)v[0];
                s->unit_wifi_ch = v[1];
                tlv_str2(v, l, 2, s->wifi_ssid, sizeof s->wifi_ssid,
                         s->wifi_ip, sizeof s->wifi_ip);
            }
            break;
        case SL2_TLV_HOMEKIT:
            if (l >= 1) {
                s->hk_paired = v[0];
                s->hk_connected = v[0] > 0;
                tlv_str2(v, l, 1, s->hk_code, sizeof s->hk_code,
                         s->hk_payload, sizeof s->hk_payload);
            }
            break;
        case SL2_TLV_OUTSIDE_T:
            if (l >= 2) {
                int16_t dc; memcpy(&dc, v, 2);
                s->outside_valid = true;
                s->outside_temp = sl2_dc_to_display(dc, use_f, ftab_id);
            }
            break;
        case SL2_TLV_COMPRESSOR:
            if (l >= 4) {
                s->comp_freq_hz = v[0];
                s->comp_stage   = stage_str(v[1]);
                switch (v[2]) {
                    case 0x02: s->sub_mode = "DEFROST"; break;
                    case 0x04: s->sub_mode = "PREHEAT"; break;
                    case 0x08: s->sub_mode = "STANDBY"; break;
                    default: break;
                }
                s->auto_sub_mode = v[3];
            }
            break;
        case SL2_TLV_SENSOR_BATT:
            if (l >= 1) s->sensor_batt_pct = v[0];
            break;
        case SL2_TLV_ROOM_SRC:
            if (l >= 2) { s->room_src = v[0]; s->room_src_status = v[1]; }
            break;
        case SL2_TLV_FW_INFO:
            tlv_str2(v, l, 0, s->unit_fw, sizeof s->unit_fw,
                     s->unit_build, sizeof s->unit_build);
            s->diag_seen = true;
            break;
        case SL2_TLV_RUNTIME:
            if (l >= 4) {
                uint32_t h; memcpy(&h, v, 4);
                s->runtime_h = h;
                s->runtime_valid = true;
            }
            break;
        case SL2_TLV_SYS:
            if (l >= 5) {
                uint32_t up; memcpy(&up, v, 4);
                s->unit_uptime_s = up;
                s->unit_reset_reason = v[4];
                s->diag_seen = true;
            }
            break;
        default:
            break;
        }
    }
}
