// SPDX-License-Identifier: GPL-3.0-only
/* sim_main.c — mirrors dial_main.c wiring on the PC. */
#include "bsp.h"
#include "ui.h"
#include "sim.h"
#include "sim_time.h"
#include "sim_shot.h"
#include "sim_cmd.h"
#include "prefs.h"
#include "lvgl.h"
#include <SDL2/SDL.h>
#ifndef __EMSCRIPTEN__
#include <sys/select.h>
#endif
#include <stdio.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

bool sim_headless = false;
volatile bool sim_quit = false;

/* The dial would transmit an ESP-NOW CMD here — print it instead. */
static void on_edit(void)
{
    hvac_state_t s; ui_get_state(&s);
    /* indexed by raw hvac_mode_t — MUST match the enum order in ui.h */
    static const char *M[] = { "OFF", "HEAT", "COOL", "HEAT_COOL", "AUTO", "DRY", "FAN" };
    printf("CMD: mode=%s set=%d fan=%d vvane=%d hvane=%d\n",
           M[s.mode], s.set_dd, s.fan_pct, s.vvane, s.hvane);
}
static void on_units(bool f)     { printf("CMD: units=%s\n", f ? "F" : "C"); }
/* The room-source picker's commit. Ids are opaque on the wire, but sim_cmd's
 * `roomsrc` mints them 1..n in list order, so the printed id is the 1-based
 * row a test asked for. */
static void on_room_source_id(uint64_t id) { printf("CMD: room_source_id=%llu\n",
                                                    (unsigned long long)id); }
static void on_pair(void)        { printf("CMD: begin pairing\n"); }
/* Faithful to the firmware: espnow_client's pair_task answers a cancel by winding
 * the search down and reporting PAIR_UI_PROMPT (its `cancelled:` label). Printing
 * alone would have hidden the bug where that PROMPT cleared the dial's bond and
 * stranded a cancelled add-zone on the ready-to-pair face. */
static void on_pair_cancel(void) { printf("CMD: cancel pairing\n"); ui_set_pairing(PAIR_UI_PROMPT, 0); }
static void on_forget(void)      { printf("CMD: forget bond\n"); }
static void on_enc(int d)        { ui_encoder(d); }
static void on_btn(void)         { ui_button(); }
static void on_long(void)        { ui_button_long(); }

#ifdef __EMSCRIPTEN__
/* JS bridge: the page's controls and raw-command box call this via ccall. */
EMSCRIPTEN_KEEPALIVE int sim_web_cmd(const char *line) { return sim_cmd_exec(line); }
static void web_tick(void) { lv_timer_handler(); }
#endif

int main(int argc, char **argv)
{
    /* Parse native command-line options. */
    const char *run = NULL, *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) sim_headless = true;
        else if (!strcmp(argv[i], "--run") && i + 1 < argc) run = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else {
            fprintf(stderr, "usage: serin-sim [--headless] [--run \"cmd;cmd\"] [--script file]\n");
            return 2;
        }
    }
    /* Headless boots mark the first-run coach as SEEN: every test row starts
     * from `rm -f .prefs`, and an unsuppressed coach would pop over every
     * golden face ~1s after the splash. `coach arm` (sim_cmd) re-arms the
     * real trigger for the rows that test it; interactive runs keep the true
     * first-run behavior (delete ./.prefs to feel it again). */
    if (sim_headless) prefs_set_bool("coach", true);
    sim_time_init(sim_headless);
    lv_init();
    lv_tick_set_cb(sim_time_ms);
    bsp_display_start();
    bsp_backlight_set(100);

    ui_init();
    ui_set_on_edit(on_edit);
    ui_set_on_units(on_units);
    ui_set_on_room_source_id(on_room_source_id);
    ui_set_on_brightness(bsp_backlight_set);
    ui_set_on_display_sleep(bsp_display_sleep);
    ui_set_on_pair(on_pair);
    ui_set_on_pair_cancel(on_pair_cancel);
    ui_set_on_forget(on_forget);
    ui_set_on_wifi_setup(sim_cmd_on_wifi_setup);
    ui_set_on_wifi_setup_session(sim_wifi_session_start);
    ui_set_on_wifi_cancel(sim_wifi_cancel);
    ui_set_fw_version("sim");
    ui_set_fw_build(__DATE__);

    /* Healthy linked default (device boots NOLINK; UX work wants a live face).
     * Scripts/commands can override any field. ui_set_link_state must run
     * BEFORE ui_set_state: thermo_face() reads s_link.bonded, but only
     * refresh_thermostat() (triggered by ui_set_state) re-evaluates it —
     * ui_set_link_state alone doesn't refresh the thermostat screen, so
     * calling it after ui_set_state left the home dial stuck on
     * DIAL_UNPAIRED. */
    ui_set_link_state(true, true,
                      (const uint8_t[]){ 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 }, -42);
    hvac_state_t s; ui_get_state(&s);
    s.mode = MODE_HEAT; s.status = ST_ACTIVE;
    s.room_temp = 70; s.set_dd = 720; s.use_f = true; s.humidity = 44;
    s.alert = ALERT_NONE; s.cn105_connected = true;
    /* Outdoor readings stay invalid until the controller sends SL2_TLV_OUTSIDE_T.
     * Scenarios that need an outdoor reading must set it explicitly. */
    ui_set_state(&s);

    bsp_input_start(on_enc, on_btn, on_long, ui_button_down, ui_button_up);
#ifdef __EMSCRIPTEN__
    /* Browser: no argv/stdin; the page drives sim_web_cmd(). The boot splash
     * simply plays on-canvas. emscripten_set_main_loop never returns. */
    (void)run; (void)script;
    emscripten_set_main_loop(web_tick, 0, 1);
#else
    /* ui_init() covers the first boot face with a ~1.5s brand splash
     * (fade-in 400ms + hold 700ms + fade-out 400ms, see splash_show() in
     * main/ui.c); wait it out so scripts/commands start on the home dial,
     * not mid-splash. */
    sim_time_wait(1700);

    int rc = 0;
    if (script) rc = sim_cmd_run_script(script);
    if (!rc && run) rc = sim_cmd_run_string(run);
    if (sim_headless) return rc ? 1 : 0;

    char buf[512];
    printf("sim> "); fflush(stdout);
    while (!sim_quit) {
        lv_timer_handler();
        fd_set fs; struct timeval tv = { 0, 0 };
        FD_ZERO(&fs); FD_SET(0, &fs);
        if (select(1, &fs, NULL, NULL, &tv) > 0 && fgets(buf, sizeof buf, stdin)) {
            sim_cmd_exec(buf);
            printf("sim> "); fflush(stdout);
        }
        SDL_Delay(5);
    }
#endif
    return 0;
}
