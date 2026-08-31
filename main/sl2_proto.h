/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL2_PROTO_VERSION    4
#define SL2_PROTO_MIN_COMPAT 1

#define SL2_ESPNOW_PMK "serin-link-open\0"

enum sl2_pkt_type {
    SL2_PKT_STATE     = 1,
    SL2_PKT_CMD       = 2,
    SL2_PKT_PROBE     = 3,
    SL2_PKT_PAIR_REQ  = 4,
    SL2_PKT_PAIR_RESP = 5,
    SL2_PKT_CAPS      = 6,
    SL2_PKT_INFO      = 7,
    SL2_PKT_WIFI_REQ  = 8,
    SL2_PKT_WIFI_RESP = 9,
    SL2_PKT_WIFI_SETUP = 10,
    SL2_PKT_DIAL_INFO  = 11,
    SL2_PKT_DIAL_SENSOR = 12,
    SL2_PKT_ROOM_CATALOG_REQ = 13, SL2_PKT_ROOM_CATALOG_RESP = 14,
    SL2_PKT_ROOM_SOURCE_SET = 15, SL2_PKT_ROOM_SOURCE_ACK = 16,
};

enum sl2_mode {
    SL2_MODE_OFF       = 0,
    SL2_MODE_HEAT      = 1,
    SL2_MODE_COOL      = 2,
    SL2_MODE_HEAT_COOL = 3,
    SL2_MODE_AUTO      = 4,
    SL2_MODE_DRY       = 5,
    SL2_MODE_FAN_ONLY  = 6,
};

enum sl2_action {
    SL2_ACT_UNKNOWN = 0, SL2_ACT_IDLE    = 1, SL2_ACT_HEATING = 2,
    SL2_ACT_COOLING = 3, SL2_ACT_DRYING  = 4, SL2_ACT_FAN     = 5,
    SL2_ACT_DEFROST = 6, SL2_ACT_PREHEAT = 7, SL2_ACT_STANDBY = 8,
};

enum sl2_preset {
    SL2_PRESET_NONE = 0, SL2_PRESET_ECO     = 1, SL2_PRESET_AWAY  = 2,
    SL2_PRESET_BOOST = 3, SL2_PRESET_COMFORT = 4, SL2_PRESET_HOME = 5,
    SL2_PRESET_SLEEP = 6, SL2_PRESET_ACTIVITY = 7,
};

#define SL2_FAN_AUTO   0
#define SL2_VANE_AUTO  0
#define SL2_VANE_SWING 255

#define SL2_DC_NA      ((int16_t)0x7FFF)
#define SL2_HUM_NA     ((uint8_t)0xFF)
#define SL2_CC_NA      ((int16_t)0x7FFF)
#define SL2_HUM_CC_NA  ((uint16_t)0xFFFF)

enum {
    SL2_SF_HVAC_LINK    = 1u << 0,
    SL2_SF_WIFI         = 1u << 1,
    SL2_SF_USE_F        = 1u << 2,
    SL2_SF_WIFI_PROVISIONED = 1u << 3,
    SL2_SF_SETUP_AP     = 1u << 4,
    SL2_SF_WIFI_ERR     = 1u << 5,
};
enum {
    SL2_SF2_SENSOR_BATT_LOW = 1u << 0,
    SL2_SF2_SCREEN_CTL      = 1u << 1,
    SL2_SF2_SCREEN_OFF      = 1u << 2,
    SL2_SF2_NIGHT_CTL       = 1u << 3,
    SL2_SF2_NIGHT           = 1u << 4,
};

struct __attribute__((packed)) sl2_state_pkt {
    uint8_t  type;
    uint8_t  version;
    uint8_t  caps_seq;
    uint8_t  flags;
    uint8_t  mode;
    uint8_t  action;
    uint8_t  fan;
    uint8_t  vane_v;
    uint8_t  vane_h;
    uint8_t  preset;
    uint16_t fault;
    int16_t  room_dc;
    int16_t  set_dc;
    int16_t  set_low_dc;
    int16_t  set_high_dc;
    uint8_t  room_hum_pct;
    uint8_t  hum_set_pct;
    uint8_t  flags2;
    uint8_t  night_ceil;
    uint16_t epoch;
};
#define SL2_STATE_MIN_LEN 26

enum {
    SL2_CM_MODE      = 1u << 0,
    SL2_CM_TEMP      = 1u << 1,
    SL2_CM_TEMP_BAND = 1u << 2,
    SL2_CM_FAN       = 1u << 3,
    SL2_CM_VANEV     = 1u << 4,
    SL2_CM_VANEH     = 1u << 5,
    SL2_CM_PRESET    = 1u << 6,
    SL2_CM_HUM       = 1u << 7,
    SL2_CM_UNITS     = 1u << 8,
};

struct __attribute__((packed)) sl2_cmd_pkt {
    uint8_t  type;
    uint8_t  version;
    uint16_t mask;
    uint8_t  mode;
    uint8_t  fan;
    uint8_t  vane_v;
    uint8_t  vane_h;
    uint8_t  preset;
    int16_t  set_dc;
    int16_t  set_low_dc;
    int16_t  set_high_dc;
    uint8_t  hum_set_pct;
    uint8_t  use_f;
    uint16_t epoch;
};
#define SL2_CMD_MIN_LEN 19

enum {
    SL2_WANT_INFO  = 1u << 0,
    SL2_WANT_CAPS  = 1u << 1,
    SL2_WANT_STATE = 1u << 2,
};

struct __attribute__((packed)) sl2_probe_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t want;
    uint8_t reserved[1];
};
#define SL2_PROBE_MIN_LEN 3

struct __attribute__((packed)) sl2_pair_req_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t src_mac[6];
    uint8_t eph_pub[32];
    uint8_t id_pub[32];
    uint8_t sig[64];
    uint8_t reserved[1];
};

struct __attribute__((packed)) sl2_pair_resp_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t src_mac[6];
    uint8_t eph_pub[32];
    uint8_t id_pub[32];
    uint8_t sig[64];
    uint8_t channel;
};
#define SL2_PAIR_MIN_LEN 136

#define SL2_REQ_TRANSCRIPT_LEN  (8 + 2 + 6 + 32 + 32)
#define SL2_RESP_TRANSCRIPT_LEN (9 + 2 + 6 + 32 + 32 + 32 + 1)

static inline size_t sl2_pair_req_transcript(const struct sl2_pair_req_pkt *p,
                                             uint8_t out[SL2_REQ_TRANSCRIPT_LEN]) {
    memcpy(out, "SLv2-req", 8);
    out[8] = p->type; out[9] = p->version;
    memcpy(out + 10, p->src_mac, 6);
    memcpy(out + 16, p->eph_pub, 32);
    memcpy(out + 48, p->id_pub, 32);
    return SL2_REQ_TRANSCRIPT_LEN;
}

static inline size_t sl2_pair_resp_transcript(const struct sl2_pair_resp_pkt *p,
                                              const uint8_t dial_eph_pub[32],
                                              uint8_t out[SL2_RESP_TRANSCRIPT_LEN]) {
    memcpy(out, "SLv2-resp", 9);
    out[9] = p->type; out[10] = p->version;
    memcpy(out + 11, p->src_mac, 6);
    memcpy(out + 17, p->eph_pub, 32);
    memcpy(out + 49, p->id_pub, 32);
    memcpy(out + 81, dial_eph_pub, 32);
    out[113] = p->channel;
    return SL2_RESP_TRANSCRIPT_LEN;
}

#define SL2_LMK_INFO      "serin-link-v2-lmk"
#define SL2_LMK_INFO_LEN  17
static inline void sl2_lmk_salt(const uint8_t dial_eph_pub[32],
                                const uint8_t ctrl_eph_pub[32], uint8_t out[64]) {
    memcpy(out, dial_eph_pub, 32);
    memcpy(out + 32, ctrl_eph_pub, 32);
}

enum {
    SL2_CF_HUM_CTRL = 1u << 0,
};
enum {
    SL2_FEAT_WIFI_INFO      = 1u << 0,
    SL2_FEAT_HOMEKIT        = 1u << 1,
    SL2_FEAT_OUTSIDE_T      = 1u << 2,
    SL2_FEAT_COMPRESSOR     = 1u << 3,
    SL2_FEAT_SENSOR_BATT    = 1u << 4,
    SL2_FEAT_FW_INFO        = 1u << 5,
    SL2_FEAT_RUNTIME        = 1u << 6,
    SL2_FEAT_LINK_OTA_CREDS = 1u << 7,
    SL2_FEAT_ENERGY         = 1u << 8,
    SL2_FEAT_WIFI_SETUP     = 1u << 9,
    SL2_FEAT_LINK_SENSOR    = 1u << 10,
    SL2_FEAT_SCREEN         = 1u << 11,
    SL2_FEAT_ROOM_CATALOG   = 1u << 12,
};

#define SL2_VANECAP(n_pos, has_auto, has_swing) \
    (uint8_t)(((n_pos) & 0x0F) | ((has_auto) ? 0x10 : 0) | ((has_swing) ? 0x20 : 0))

#define SL2_VANECAP_SPLIT_TOP 0x40

#define SL2_VANE_SPLIT_LEGACY 6

static inline uint8_t sl2_vanecap_npos(uint8_t v)      { return v & 0x0F; }
static inline bool    sl2_vanecap_has_auto(uint8_t v)  { return (v & 0x10) != 0; }
static inline bool    sl2_vanecap_has_swing(uint8_t v) { return (v & 0x20) != 0; }
static inline bool    sl2_vanecap_split_top(uint8_t v) {
    return (v & SL2_VANECAP_SPLIT_TOP) != 0;
}

static inline uint8_t sl2_vanecap_split_pos(uint8_t v) {
    uint8_t n = sl2_vanecap_npos(v);
    if (sl2_vanecap_split_top(v)) return n;
    return n >= SL2_VANE_SPLIT_LEGACY ? SL2_VANE_SPLIT_LEGACY : 0;
}

struct __attribute__((packed)) sl2_caps_pkt {
    uint8_t  type;
    uint8_t  version;
    uint8_t  caps_seq;
    uint8_t  caps_flags;
    uint16_t modes;
    uint16_t presets;
    uint8_t  fan_steps;
    uint8_t  fan_flags;
    uint8_t  vane_v;
    uint8_t  vane_h;
    int16_t  set_min_dc;
    int16_t  set_max_dc;
    uint8_t  set_step_dc;
    uint8_t  ftab_id;
    uint8_t  band_min_gap_dc;
    uint8_t  hum_step_pct;
    uint16_t features;
    char     name[32];
    uint8_t  reserved[14];
};
#define SL2_CAPS_MIN_LEN 54
#define SL2_FAN_HAS_AUTO (1u << 0)

struct __attribute__((packed)) sl2_info_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t reserved[2];
};
#define SL2_INFO_HDR_LEN 4

enum sl2_tlv_type {
    SL2_TLV_WIFI_INFO   = 0x01,
    SL2_TLV_HOMEKIT     = 0x02,
    SL2_TLV_OUTSIDE_T   = 0x03,
    SL2_TLV_COMPRESSOR  = 0x04,
    SL2_TLV_SENSOR_BATT = 0x05,
    SL2_TLV_FW_INFO     = 0x06,
    SL2_TLV_RUNTIME     = 0x07,
    SL2_TLV_SYS         = 0x08,
    SL2_TLV_ENERGY      = 0x09,
    SL2_TLV_ROOM_SRC    = 0x0B,
    SL2_TLV_ROOM_SOURCE_V2 = 0x0C,
};

static inline bool sl2_tlv_put(uint8_t *buf, size_t cap, size_t *off,
                               uint8_t t, const void *v, uint8_t l) {
    if (*off + 2 + (size_t)l > cap) return false;
    buf[*off] = t; buf[*off + 1] = l;
    if (l) memcpy(buf + *off + 2, v, l);
    *off += 2 + (size_t)l;
    return true;
}

static inline bool sl2_tlv_next(const uint8_t *buf, size_t len, size_t *off,
                                uint8_t *t, uint8_t *l, const uint8_t **v) {
    if (*off + 2 > len) return false;
    uint8_t tt = buf[*off], ll = buf[*off + 1];
    if (*off + 2 + (size_t)ll > len) return false;
    *t = tt; *l = ll; *v = buf + *off + 2;
    *off += 2 + (size_t)ll;
    return true;
}

#define SL2_ROOM_SOURCE_INTERNAL_ID  UINT64_C(0)
#define SL2_ROOM_SOURCE_AVERAGE_ID   UINT64_C(1)
#define SL2_ROOM_SOURCE_NS_SENSOR    2u
#define SL2_ROOM_SOURCE_NS_LINK      1u
#define SL2_ROOM_SOURCE_NS_LINK_AUTO 3u
#define SL2_ROOM_SOURCE_LINK_AUTO_ID (UINT64_C(SL2_ROOM_SOURCE_NS_LINK_AUTO) << 56)
#define SL2_ROOM_SOURCE_NAME_LEN     24
#define SL2_ROOM_CATALOG_DONE        UINT16_C(0xFFFF)

#define SL2_ROOM_CATALOG_PAGE_MAX    7

#define SL2_ROOM_CATALOG_MAX         12

static inline uint64_t sl2_room_source_mac_id(uint8_t ns, const uint8_t mac[6]) {
    uint64_t id = (uint64_t)ns << 56;
    for (int i = 0; i < 6; i++) id |= (uint64_t)mac[i] << (40 - i * 8);
    return id;
}

enum sl2_room_source_kind {
    SL2_ROOM_KIND_INTERNAL  = 0,
    SL2_ROOM_KIND_SENSOR    = 1,
    SL2_ROOM_KIND_LINK      = 2,
    SL2_ROOM_KIND_AUTO      = 3,
    SL2_ROOM_KIND_AGGREGATE = 4,
};

enum {
    SL2_ROOM_SOURCE_F_SELECTABLE = 1u << 0,
};

struct __attribute__((packed)) sl2_room_source_entry {
    uint64_t id;
    uint8_t  kind;
    uint8_t  flags;
    char     name[SL2_ROOM_SOURCE_NAME_LEN];
};

struct __attribute__((packed)) sl2_room_catalog_req_pkt {
    uint8_t  type;
    uint8_t  version;
    uint16_t cursor;
    uint32_t known_revision;
};

struct __attribute__((packed)) sl2_room_catalog_resp_pkt {
    uint8_t  type;
    uint8_t  version;
    uint8_t  count;
    uint8_t  flags;
    uint16_t next_cursor;
    uint16_t reserved;
    uint32_t revision;
    struct sl2_room_source_entry entries[SL2_ROOM_CATALOG_PAGE_MAX];
};
#define SL2_ROOM_CATALOG_RESP_HDR_LEN 12

struct __attribute__((packed)) sl2_room_catalog_resp_hdr {
    uint8_t  type;
    uint8_t  version;
    uint8_t  count;
    uint8_t  flags;
    uint16_t next_cursor;
    uint16_t reserved;
    uint32_t revision;
};

struct __attribute__((packed)) sl2_room_source_set_pkt {
    uint8_t  type;
    uint8_t  version;
    uint8_t  request_id;
    uint8_t  reserved;
    uint32_t revision;
    uint64_t source_id;
};

enum sl2_room_source_result {
    SL2_ROOM_SET_OK            = 0,
    SL2_ROOM_SET_BAD_SOURCE    = 1,
    SL2_ROOM_SET_STALE_CATALOG = 2,
    SL2_ROOM_SET_UNSUPPORTED   = 3,
};

struct __attribute__((packed)) sl2_room_source_ack_pkt {
    uint8_t  type;
    uint8_t  version;
    uint8_t  request_id;
    uint8_t  result;
    uint32_t revision;
    uint64_t source_id;
    uint8_t  status;
    uint8_t  reserved[3];
};
#define SL2_ROOM_SOURCE_ACK_MIN_LEN 20

struct __attribute__((packed)) sl2_room_source_v2 {
    uint32_t revision;
    uint64_t source_id;
    uint8_t  status;
};

static inline bool sl2_info_put_room_source_v2(uint8_t *b, size_t c, size_t *o,
                                               uint32_t rev, uint64_t id, uint8_t st) {
    struct sl2_room_source_v2 v = { rev, id, st };
    return sl2_tlv_put(b, c, o, SL2_TLV_ROOM_SOURCE_V2, &v, sizeof v);
}

struct __attribute__((packed)) sl2_wifi_req_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t reserved[2];
};
#define SL2_WIFI_REQ_MIN_LEN 4

struct __attribute__((packed)) sl2_wifi_resp_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t ok;
    char    ssid[33];
    char    psk[65];
    uint8_t reserved[2];
};
#define SL2_WIFI_RESP_MIN_LEN 103

struct __attribute__((packed)) sl2_wifi_setup_pkt {
    uint8_t  type;
    uint8_t  version;
    uint16_t epoch;
};
#define SL2_WIFI_SETUP_MIN_LEN 4

struct __attribute__((packed)) sl2_dial_info_pkt {
    uint8_t type;
    uint8_t version;
    uint8_t caps_seq;
    char    model[24];
    char    fw[16];
};
#define SL2_DIAL_INFO_MIN_LEN 3

enum {
    SL2_DSF_HAS_SENSOR = 1u << 0,
    SL2_DSF_SCREEN_VALID = 1u << 1,
    SL2_DSF_SCREEN_ON    = 1u << 2,
};

enum sl2_room_src {
    SL2_ROOMSRC_INTERNAL = 0,
    SL2_ROOMSRC_BLE      = 1,
    SL2_ROOMSRC_LINK     = 2,
    SL2_ROOMSRC_NOEDIT   = 0xFF,
};

enum sl2_room_status {
    SL2_ROOMST_OK          = 0,
    SL2_ROOMST_STALE       = 1,
    SL2_ROOMST_UNAVAILABLE = 2,
};

struct __attribute__((packed)) sl2_dial_sensor_pkt {
    uint8_t  type;
    uint8_t  version;
    uint8_t  flags;
    int16_t  temp_cc;
    uint16_t hum_cc;
    uint8_t  want_src;
    uint8_t  reserved[1];
};
#define SL2_DIAL_SENSOR_MIN_LEN 7
#define SL2_DIAL_SENSOR_MIN_VER 3

#define SL2_STATIC_ASSERT(c, m) typedef char sl2_sa_##m[(c) ? 1 : -1]
SL2_STATIC_ASSERT(sizeof(struct sl2_state_pkt)     == 26,  state_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_cmd_pkt)       == 19,  cmd_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_probe_pkt)     == 4,   probe_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_pair_req_pkt)  == 137, pair_req_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_pair_resp_pkt) == 137, pair_resp_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_caps_pkt)      == 68,  caps_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_info_pkt)      == 4,   info_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_wifi_req_pkt)  == 4,   wifi_req_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_wifi_resp_pkt) == 103, wifi_resp_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_wifi_setup_pkt) == 4,  wifi_setup_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_dial_info_pkt) == 43,  dial_info_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_dial_sensor_pkt) == 9, dial_sensor_size);
SL2_STATIC_ASSERT(sizeof(struct sl2_room_catalog_resp_pkt) == 250, room_catalog_resp_size);
SL2_STATIC_ASSERT(SL2_ROOM_CATALOG_RESP_HDR_LEN ==
                  offsetof(struct sl2_room_catalog_resp_pkt, entries), room_catalog_hdr_len);
SL2_STATIC_ASSERT(sizeof(struct sl2_room_catalog_resp_hdr) ==
                  SL2_ROOM_CATALOG_RESP_HDR_LEN, room_catalog_hdr_size);
SL2_STATIC_ASSERT(offsetof(struct sl2_room_catalog_resp_hdr, revision) ==
                  offsetof(struct sl2_room_catalog_resp_pkt, revision), room_catalog_hdr_match);
SL2_STATIC_ASSERT(SL2_ROOM_SOURCE_ACK_MIN_LEN <= (int)sizeof(struct sl2_room_source_ack_pkt),
                  room_source_ack_minlen);
SL2_STATIC_ASSERT(SL2_DIAL_INFO_MIN_LEN <= (int)sizeof(struct sl2_dial_info_pkt), dial_info_minlen);
SL2_STATIC_ASSERT(SL2_DIAL_SENSOR_MIN_LEN <= (int)sizeof(struct sl2_dial_sensor_pkt), dial_sensor_minlen);
SL2_STATIC_ASSERT(SL2_STATE_MIN_LEN <= (int)sizeof(struct sl2_state_pkt), state_minlen);
SL2_STATIC_ASSERT(SL2_CMD_MIN_LEN   <= (int)sizeof(struct sl2_cmd_pkt),   cmd_minlen);
SL2_STATIC_ASSERT(SL2_PROBE_MIN_LEN <= (int)sizeof(struct sl2_probe_pkt), probe_minlen);
SL2_STATIC_ASSERT(SL2_CAPS_MIN_LEN  <= (int)sizeof(struct sl2_caps_pkt),  caps_minlen);
SL2_STATIC_ASSERT(SL2_PAIR_MIN_LEN  <= (int)sizeof(struct sl2_pair_req_pkt), pair_minlen);

static inline void sl2_decode_pkt(void *dst, size_t dstsz,
                                  const void *src, int len) {
    memset(dst, 0, dstsz);
    size_t n = (size_t)len < dstsz ? (size_t)len : dstsz;
    memcpy(dst, src, n);
}

static inline int16_t sl2_c_to_dc(float c)    { return (int16_t)lroundf(c * 10.0f); }
static inline float   sl2_dc_to_c(int16_t dc) { return (float)dc / 10.0f; }

#define SL2_FTAB1_MIN_F 61
#define SL2_FTAB1_MAX_F 88
static inline const float *sl2_ftab1(void) {
    static const float tab[SL2_FTAB1_MAX_F - SL2_FTAB1_MIN_F + 1] = {
        16.0f,16.5f,17.0f,17.5f,18.0f,18.5f,19.0f,20.0f,21.0f,21.5f,
        22.0f,22.5f,23.0f,23.5f,24.0f,24.5f,25.0f,25.5f,26.0f,26.5f,
        27.0f,27.5f,28.0f,28.5f,29.0f,29.5f,30.0f,30.5f,
    };
    return tab;
}
static inline float sl2_ftab1_f_to_c(int f) {
    if (f < SL2_FTAB1_MIN_F) f = SL2_FTAB1_MIN_F;
    if (f > SL2_FTAB1_MAX_F) f = SL2_FTAB1_MAX_F;
    return sl2_ftab1()[f - SL2_FTAB1_MIN_F];
}
static inline int sl2_ftab1_c_to_f(float c) {
    const float *tab = sl2_ftab1();
    int bestF = SL2_FTAB1_MIN_F;
    float bestD = 999.0f;
    for (int i = 0; i <= SL2_FTAB1_MAX_F - SL2_FTAB1_MIN_F; i++) {
        float d = fabsf(tab[i] - c);
        if (d < bestD) { bestD = d; bestF = SL2_FTAB1_MIN_F + i; }
    }
    return bestF;
}

static inline int sl2_dc_to_display(int16_t dc, bool use_f, uint8_t ftab_id) {
    float c = sl2_dc_to_c(dc);
    if (!use_f) return (int)lroundf(c);
    if (ftab_id == 1 && c >= 16.0f && c <= 30.5f) return sl2_ftab1_c_to_f(c);
    return (int)lroundf(c * 9.0f / 5.0f + 32.0f);
}

static inline int16_t sl2_display_to_dc(int v, bool use_f, uint8_t ftab_id) {
    if (!use_f) return (int16_t)((int)lroundf((float)v * 2.0f) * 5);
    if (ftab_id == 1) return sl2_c_to_dc(sl2_ftab1_f_to_c(v));
    return sl2_c_to_dc(((float)v - 32.0f) * 5.0f / 9.0f);
}

static inline bool sl2_mac_eq(const uint8_t a[6], const uint8_t b[6]) {
    return memcmp(a, b, 6) == 0;
}

#ifdef __cplusplus
}
#endif
