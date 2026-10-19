/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include "sl2_proto.h"

static void vector(const char *name, const void *data, size_t len) {
    const uint8_t *p = data;
    printf("vector %s =", name);
    for (size_t i = 0; i < len; ++i) printf(" %02x", p[i]);
    putchar('\n');
}

static void wire_vectors(void) {
    /* Independent little-endian bytes pin units, signed values, masks,
     * sentinels, and the replay epoch, beyond matching struct dimensions. */
    const struct sl2_state_pkt state = {
        .type = SL2_PKT_STATE, .version = SL2_PROTO_VERSION, .caps_seq = 0x31,
        .flags = SL2_SF_HVAC_LINK | SL2_SF_WIFI, .mode = SL2_MODE_HEAT_COOL,
        .action = SL2_ACT_HEATING, .fan = 75, .vane_v = 2,
        .vane_h = SL2_VANE_SWING, .preset = SL2_PRESET_ECO, .fault = 0x1234,
        .room_dc = -123, .set_dc = SL2_DC_NA, .set_low_dc = 185,
        .set_high_dc = 245, .room_hum_pct = 43, .hum_set_pct = SL2_HUM_NA,
        .flags2 = SL2_SF2_NIGHT_CTL | SL2_SF2_NIGHT,
        .night_ceil = 20, .epoch = 0xabcd,
    };
    const uint8_t state_bytes[] = {
        1,5,0x31,3,3,2,75,2,255,1,0x34,0x12,0x85,0xff,
        0xff,0x7f,0xb9,0,0xf5,0,43,255,24,20,0xcd,0xab,
    };
    assert(sizeof state == sizeof state_bytes);
    assert(memcmp(&state, state_bytes, sizeof state) == 0);
    vector("state", &state, sizeof state);

    const struct sl2_cmd_pkt cmd = {
        .type = SL2_PKT_CMD, .version = SL2_PROTO_VERSION,
        .mask = SL2_CM_TEMP_BAND | SL2_CM_UNITS, .mode = SL2_MODE_HEAT_COOL,
        .fan = 25, .vane_v = SL2_VANE_AUTO, .vane_h = 3,
        .preset = SL2_PRESET_SLEEP, .set_dc = 225, .set_low_dc = -55,
        .set_high_dc = 300, .hum_set_pct = 50, .use_f = 1, .epoch = 0xabcd,
    };
    const uint8_t cmd_bytes[] = {
        2,5,4,1,3,25,0,3,6,0xe1,0,0xc9,0xff,0x2c,1,50,1,0xcd,0xab,
    };
    assert(sizeof cmd == sizeof cmd_bytes);
    assert(memcmp(&cmd, cmd_bytes, sizeof cmd) == 0);
    vector("cmd", &cmd, sizeof cmd);

    const struct sl2_dial_sensor_pkt sensor = {
        .type = SL2_PKT_DIAL_SENSOR, .version = SL2_PROTO_VERSION,
        .flags = SL2_DSF_HAS_SENSOR | SL2_DSF_SCREEN_VALID | SL2_DSF_SCREEN_ON,
        .temp_cc = -1234, .hum_cc = 4567, .want_src = SL2_ROOMSRC_NOEDIT,
        .epoch = 0xabcd,
    };
    const uint8_t sensor_bytes[] = {12,5,7,0x2e,0xfb,0xd7,0x11,255,0,0xcd,0xab};
    assert(sizeof sensor == sizeof sensor_bytes);
    assert(memcmp(&sensor, sensor_bytes, sizeof sensor) == 0);
    vector("dial_sensor", &sensor, sizeof sensor);

    const struct sl2_caps_pkt caps = {
        .type = SL2_PKT_CAPS, .version = SL2_PROTO_VERSION, .caps_seq = 0x31,
        .caps_flags = SL2_CF_HUM_CTRL,
        .modes = (1u << SL2_MODE_HEAT) | (1u << SL2_MODE_HEAT_COOL),
        .presets = 1u << SL2_PRESET_ECO, .fan_steps = 4,
        .fan_flags = SL2_FAN_HAS_AUTO, .vane_v = SL2_VANECAP(5, true, true),
        .vane_h = SL2_VANECAP(6, false, true) | SL2_VANECAP_SPLIT_TOP,
        .set_min_dc = 160, .set_max_dc = 310, .set_step_dc = 5,
        .ftab_id = 1, .band_min_gap_dc = 20, .hum_step_pct = 5,
        .features = SL2_FEAT_LINK_SENSOR | SL2_FEAT_ROOM_CATALOG,
        .name = "Public controller",
    };
    vector("caps", &caps, sizeof caps);

    const uint8_t mac[] = {0x02,0x11,0x22,0x33,0x44,0x55};
    uint64_t source = sl2_room_source_mac_id(SL2_ROOM_SOURCE_NS_LINK, mac);
    assert(source == UINT64_C(0x0100021122334455));
    const struct sl2_room_source_set_pkt set = {
        .type = SL2_PKT_ROOM_SOURCE_SET, .version = SL2_PROTO_VERSION,
        .request_id = 9, .revision = 0x12345678, .source_id = source,
        .epoch = 0xabcd,
    };
    vector("room_source_set", &set, sizeof set);
    const struct sl2_room_catalog_resp_pkt catalog = {
        .type = SL2_PKT_ROOM_CATALOG_RESP, .version = SL2_PROTO_VERSION,
        .count = 1, .next_cursor = SL2_ROOM_CATALOG_DONE, .revision = 0x12345678,
        .entries = {{.id = source, .kind = SL2_ROOM_KIND_LINK,
                     .flags = SL2_ROOM_SOURCE_F_SELECTABLE, .name = "Link 1"}},
    };
    vector("room_catalog", &catalog, sizeof catalog);
    const struct sl2_room_source_v2 room = {
        .revision = 0x12345678, .source_id = source, .status = SL2_ROOMST_STALE,
    };
    uint8_t info[32] = {0};
    size_t off = SL2_INFO_HDR_LEN;
    info[0] = SL2_PKT_INFO; info[1] = SL2_PROTO_VERSION;
    assert(sl2_tlv_put(info, sizeof info, &off, SL2_TLV_ROOM_SOURCE_V2,
                       &room, sizeof room));
    vector("info_room_source_tlv", info, off);
    size_t cursor = SL2_INFO_HDR_LEN;
    uint8_t type, len;
    const uint8_t *value;
    assert(sl2_tlv_next(info, off, &cursor, &type, &len, &value));
    assert(type == SL2_TLV_ROOM_SOURCE_V2 && len == sizeof room);
    assert(memcmp(value, &room, len) == 0 && cursor == off);

    const struct sl2_wifi_resp_pkt wifi = {
        .type = SL2_PKT_WIFI_RESP, .version = SL2_PROTO_VERSION,
        .ok = 1, .ssid = "Test SSID", .psk = "test-only-password",
    };
    vector("wifi_credentials", &wifi, sizeof wifi);
    const struct sl2_wifi_setup_session_pkt setup = {
        .type = SL2_PKT_WIFI_SETUP, .version = SL2_PROTO_VERSION,
        .epoch = 0xabcd, .session = 0x12345678,
    };
    vector("wifi_setup_session", &setup, sizeof setup);
    const struct sl2_wifi_cancel_pkt cancel = {
        .type = SL2_PKT_WIFI_CANCEL, .version = SL2_PROTO_VERSION,
        .epoch = 0xabcd, .session = 0x12345678,
    };
    vector("wifi_cancel", &cancel, sizeof cancel);
    const struct sl2_wifi_cancel_ack_pkt ack = {
        .type = SL2_PKT_WIFI_CANCEL_ACK, .version = SL2_PROTO_VERSION,
        .session = 0x12345678, .status = SL2_WIFI_CANCEL_STALE,
    };
    vector("wifi_cancel_ack", &ack, sizeof ack);

    /* Distinct nonzero keys make role/order/transcript-domain changes visible.
     * These are deterministic test data, not real identities or signatures. */
    struct sl2_pair_req_pkt req = {.type = SL2_PKT_PAIR_REQ, .version = SL2_PROTO_VERSION};
    struct sl2_pair_resp_pkt resp = {
        .type = SL2_PKT_PAIR_RESP, .version = SL2_PROTO_VERSION, .channel = 11,
    };
    memcpy(req.src_mac, mac, sizeof mac);
    memcpy(resp.src_mac, mac, sizeof mac);
    resp.src_mac[5] = 0x66;
    for (size_t i = 0; i < 32; ++i) {
        req.eph_pub[i] = (uint8_t)i; req.id_pub[i] = (uint8_t)(i + 32);
        resp.eph_pub[i] = (uint8_t)(i + 64); resp.id_pub[i] = (uint8_t)(i + 96);
    }
    for (size_t i = 0; i < 64; ++i) {
        req.sig[i] = (uint8_t)(i + 128); resp.sig[i] = (uint8_t)(i + 192);
    }
    vector("pair_req", &req, sizeof req);
    vector("pair_resp", &resp, sizeof resp);
    uint8_t transcript[SL2_RESP_TRANSCRIPT_LEN];
    size_t n = sl2_pair_req_transcript(&req, transcript);
    assert(n == SL2_REQ_TRANSCRIPT_LEN);
    vector("pair_req_transcript", transcript, n);
    n = sl2_pair_resp_transcript(&resp, req.eph_pub, transcript);
    assert(n == SL2_RESP_TRANSCRIPT_LEN);
    vector("pair_resp_transcript", transcript, n);
    uint8_t salt[64];
    sl2_lmk_salt(req.eph_pub, resp.eph_pub, salt);
    vector("lmk_salt", salt, sizeof salt);
    struct sl2_pair_auth_pkt auth = {
        .type = SL2_PKT_PAIR_CONFIRM, .version = SL2_PROTO_VERSION,
    };
    memcpy(auth.tag, resp.id_pub, sizeof auth.tag);
    vector("pair_confirm", &auth, sizeof auth);
    auth.type = SL2_PKT_PAIR_ACK;
    vector("pair_ack", &auth, sizeof auth);

    /* Prefix decode is wire behavior too: missing tails zero-fill. */
    struct sl2_dial_sensor_pkt short_sensor;
    sl2_decode_pkt(&short_sensor, sizeof short_sensor, &sensor, SL2_DIAL_SENSOR_MIN_LEN);
    assert(short_sensor.want_src == 0 && short_sensor.epoch == 0);
    vector("short_sensor_decode", &short_sensor, sizeof short_sensor);
}

int main(void) {
    const uint16_t endian = 1;
    assert(*(const uint8_t *)&endian == 1 && "wire probes require a little-endian host");
#define LAYOUT(s) printf("size " #s " = %zu\n", sizeof(struct s));
#define FIELD(s, f) printf("field " #s "." #f " = %zu,%zu\n", \
                           offsetof(struct s, f), sizeof(((struct s *)0)->f));
#define CONSTANT(c) printf("constant " #c " = %" PRIu64 "\n", (uint64_t)(c));
#include "wire_contract.def"
#undef LAYOUT
#undef FIELD
#undef CONSTANT
    vector("pmk", SL2_ESPNOW_PMK, 16);
    vector("lmk_info", SL2_LMK_INFO, SL2_LMK_INFO_LEN);
    wire_vectors();
    return 0;
}
