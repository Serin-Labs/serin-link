/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#if defined(__has_include)
#  if __has_include("sdkconfig.h")
#    include "sdkconfig.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MODE_OFF, MODE_HEAT, MODE_COOL, MODE_HEAT_COOL, MODE_AUTO,
               MODE_DRY, MODE_FAN } hvac_mode_t;
typedef enum { ST_OFF, ST_IDLE, ST_ACTIVE } hvac_status_t;
typedef enum { VANE_AUTO, VANE_1, VANE_2, VANE_3, VANE_4, VANE_5, VANE_SWING } vvane_t;
typedef enum { HVANE_AUTO, HVANE_LEFT, HVANE_CLEFT, HVANE_CENTER,
               HVANE_CRIGHT, HVANE_RIGHT, HVANE_SWING } hvane_t;
#define SL2_UI_VANE_MAX_POS 5
_Static_assert(VANE_SWING  - VANE_1     == SL2_UI_VANE_MAX_POS, "vvane_t run");
_Static_assert(HVANE_SWING - HVANE_LEFT == SL2_UI_VANE_MAX_POS, "hvane_t run");

typedef enum { ALERT_NONE, ALERT_NOLINK, ALERT_PENDING, ALERT_FAULT, ALERT_OFFLINE, ALERT_SYNCING } hvac_alert_t;

typedef enum {
    SCREEN_THERMOSTAT = 0,
    SCREEN_FANVANE,
    SCREEN_MENU,
    SCREEN_MODE,
    SCREEN_SETTINGS,
    SCREEN_UPDATE,
    SCREEN_ABOUT,
    SCREEN_WIFI,
    SCREEN_ZONES,
    SCREEN_ROOM_SOURCE,
    SCREEN_ADVANCED,
    SCREEN_COUNT
} screen_id_t;

typedef struct {
    hvac_mode_t   mode;
    hvac_status_t status;
    int           room_temp;
    int           set_dd;
    int           set_lo_dd;
    int           set_hi_dd;
    bool          band_valid;
    bool          use_f;
    int           humidity;
    bool          humidity_valid;
    hvac_alert_t  alert;

    uint8_t       fan_pct;
    vvane_t       vvane;
    hvane_t       hvane;

    int           comp_freq_hz;
    const char   *comp_stage;
    const char   *sub_mode;
    int           outside_temp;
    bool          outside_valid;

    bool          cn105_connected;
    int           poll_ms;
    uint32_t      err_count;
    uint16_t      error_code;

    bool          sensor_batt_low;
    int           sensor_batt_pct;

    char          hk_code[16];
    int           hk_paired;
    bool          hk_connected;

    bool          wifi_connected;
    char          wifi_ssid[33];
    char          wifi_ip[16];
    int           wifi_rssi;

    bool          wifi_provisioned;
    bool          wifi_setup_avail;
    bool          wifi_setup_ap;
    char          hk_payload[24];

    bool          diag_seen;
    char          unit_fw[24];
    char          unit_build[12];
    uint32_t      unit_uptime_s;
    uint8_t       unit_reset_reason;
    uint8_t       unit_wifi_ch;
    uint8_t       auto_sub_mode;
    uint32_t      runtime_h;
    bool          runtime_valid;
    bool          ble_valid;
    int16_t       ble_temp_dc;
    uint8_t       ble_hum_pct;
    int8_t        ble_rssi;

    uint8_t       room_src;
    uint8_t       room_src_status;
    char          room_src_name[24];

    bool          link_sens_present;
    bool          link_sens_valid;
    int16_t       link_temp_dc;
    uint8_t       link_hum_pct;
} hvac_state_t;

static inline bool hvac_state_plausible(const hvac_state_t *s) {
    return s->set_dd > 0 || (s->band_valid && s->set_hi_dd > 0);
}

void        ui_init(void);
void        ui_set_state(const hvac_state_t *s);
/* Publish known canonical coordinates; SL2_DC_NA leaves a coordinate unchanged.
 * Caller holds the existing UI lock, as for ui_set_state. */
void        ui_set_state_canonical(const hvac_state_t *s, int set_dc, int low_dc, int high_dc);
void        ui_get_state(hvac_state_t *out);
/* Read the normalized canonical coordinates used by the edit rails.
 * All outputs are required. Caller holds the existing UI lock. */
void        ui_get_setpoints_dc(int *set_dc, int *low_dc, int *high_dc);
int         ui_band_edge(void);
int         ui_active_zone(void);
int         ui_zone_count(void);
int         ui_glancing(void);
int         ui_disp_asleep(void);
bool        ui_has_rendered(void);
int  ui_coach_active(void);
void ui_coach_arm(void);
int         ui_hold_arc_shown(void);
const char *ui_dial_face(void);
int         ui_settings_sel(void);
int         ui_adv_sel(void);
bool        ui_units_c(void);
int         ui_child_lock_enabled(void);
int         ui_child_lock_window_open(void);
bool        ui_adv_editing_units(void);
const char *ui_dlg_state(void);

void        ui_show_screen(screen_id_t id);
/* Retrace one step of the trail ui_show_screen keeps, so a Back control lands
 * on whichever screen opened this one and falls through to the thermostat
 * when nothing did. */
void        ui_back(void);
screen_id_t ui_back_target(void);
screen_id_t ui_current_screen(void);

void        ui_encoder(int delta);
void        ui_button(void);
void        ui_button_long(void);

void        ui_set_on_edit(void (*cb)(void));

void        ui_set_on_brightness(void (*cb)(int pct));
/* Optional: scale the panel's light 0-255 on top of the brightness level.
 * When set, screen transitions dip the light instead of fading pixels. */
void        ui_set_on_bright_scale(void (*cb)(uint8_t scale));
void        ui_dip_ms_set(uint32_t out_ms, uint32_t in_ms);
void        ui_dip_ms_get(uint32_t *out_ms, uint32_t *in_ms);

void        ui_set_on_display_sleep(void (*cb)(bool sleep));

typedef enum { UI_SCREEN_GATE_NONE = 0, UI_SCREEN_GATE_ON, UI_SCREEN_GATE_OFF } ui_screen_gate_t;
void        ui_set_screen_gate(ui_screen_gate_t gate);

void        ui_set_night(bool night, uint8_t ceil_pct);
bool        ui_night_active(void);

void        ui_set_on_units(void (*cb)(bool use_f));

void        ui_set_link_state(bool bonded, bool online, const uint8_t mac[6], int rssi);

void        ui_set_on_pair(void (*cb)(void));
void        ui_set_on_pair_cancel(void (*cb)(void));

void        ui_button_down(void);
void        ui_button_up(void);
void        ui_set_on_forget(void (*cb)(void));

void        ui_set_on_wifi_setup(void (*cb)(void));
void        ui_set_on_wifi_setup_session(void (*cb)(uint32_t session));
void        ui_set_on_wifi_cancel(void (*cb)(uint32_t session));
/* Call under the LVGL lock; stale sessions and inactive wizards are ignored. */
void        ui_wifi_cancel_result(uint32_t session, uint8_t status);
const char *ui_wifi_face(void);

#define UI_MAX_ZONES 7
#include "zone_badge.h"
typedef struct {
    char            name[32];
    uint8_t         mac[6];
    bool            have_state;
    int             room_temp;
    bool            use_f;
    hvac_mode_t     mode;
    hvac_status_t   status;
    int             set_dd;
    int             set_lo_dd;
    int             set_hi_dd;
    bool            band_valid;
    ui_zone_badge_t badge;
} ui_zone_info_t;
void ui_set_zones(const ui_zone_info_t *z, int n, int active);
void ui_set_on_zone_select(void (*cb)(int idx));
void ui_set_on_zones_open(void (*cb)(void));

typedef struct {
    uint16_t modes;
    uint16_t features;
    uint8_t  fan_steps;
    bool     fan_has_auto;
    uint8_t  vane_v;
    uint8_t  vane_h;
    int16_t  set_min_dc;
    int16_t  set_max_dc;
    uint8_t  set_step_dc;
    uint8_t  ftab_id;
    uint8_t  band_min_gap_dc;
} ui_caps_t;
void ui_set_caps(const ui_caps_t *caps);

void ui_set_link_sensor(bool present, bool valid, int16_t temp_dc, uint8_t hum_pct);
void ui_set_room_src(uint8_t src, uint8_t status);
void ui_set_room_src_name(const char *name);
struct sl2_room_source_entry;
void ui_set_room_sources(const struct sl2_room_source_entry *entries, int count,
                         uint64_t selected_id, uint8_t status);
void ui_set_room_source_id(uint64_t selected_id, uint8_t status);
void ui_set_on_room_source_id(void (*cb)(uint64_t source_id));

typedef enum { PAIR_UI_HIDDEN, PAIR_UI_PROMPT, PAIR_UI_SEARCHING,
               PAIR_UI_OK, PAIR_UI_FAIL, PAIR_UI_FULL,
               PAIR_UI_MISMATCH, PAIR_UI_SAVED } pair_ui_t;
void        ui_set_pairing(pair_ui_t state, uint8_t channel);

typedef enum { OTA_UI_HIDDEN, OTA_UI_CHECKING, OTA_UI_CONFIRM, OTA_UI_DOWNLOAD,
               OTA_UI_VERIFY, OTA_UI_OK, OTA_UI_UPTODATE, OTA_UI_FAIL } ota_ui_t;
void        ui_set_ota(ota_ui_t st, int pct, const char *txt);
void        ui_set_fw_version(const char *v);
void        ui_set_fw_build(const char *d);
void        ui_set_on_ota_check(void (*cb)(void));
void        ui_set_on_ota_confirm(void (*cb)(void));
void        ui_set_on_ota_cancel(void (*cb)(void));


#ifdef __cplusplus
}
#endif
