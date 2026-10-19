// SPDX-License-Identifier: GPL-3.0-only
/*
 * bsp_sim.c — PC implementation of bsp.h.
 * Headless: in-memory RGB565 framebuffer, DIRECT render mode.
 * Interactive: SDL window + mouse-as-touch + key/wheel input.
 * Button timing uses the device SERIN_HOLD_MS threshold; a hold fires once
 * while still pressed, and an earlier release produces a click.
 */
#include "bsp.h"
#include "sim.h"
#include "sim_time.h"
#include "serin_board.h"
#include "lvgl.h"
#include <SDL2/SDL.h>
#include <stdio.h>

#include "serin_input.h"
#define BTN_HOLD_MS SERIN_HOLD_MS   /* = the device BSPs' knob hold threshold */

static void (*s_on_enc)(int);
static void (*s_on_btn)(void);
static void (*s_on_hold)(void);
static void (*s_on_down)(void);
static void (*s_on_up)(void);
static lv_display_t *s_disp;

static bool s_pressed, s_hold_fired;
static uint32_t s_press_t0;

/* Fires s_on_hold once at the 700 ms mark while Space is held — same
 * semantics as iot_button BUTTON_LONG_PRESS_START on the device. */
static void hold_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_pressed && !s_hold_fired && sim_time_ms() - s_press_t0 >= BTN_HOLD_MS) {
        s_hold_fired = true;
        if (s_on_hold) s_on_hold();
    }
}

/* Headless button primitives (sim_cmd `down`/`up`): mirror the SDL SPACE path
 * so a sustained hold fires s_on_hold (= ui_button_long) at the 700 ms mark —
 * the real iot_button LONG_PRESS_START, which the raw ui_button_down/up calls
 * used to skip (letting the hold-to-confirm flows pass in the sim while they
 * broke on-device). The hold_timer_cb (created in every mode) does the firing
 * during the script's `wait`; sim_cmd's `press` owns the short-release click. */
void sim_press_down(void)
{
    s_pressed = true; s_hold_fired = false; s_press_t0 = sim_time_ms();
    if (s_on_down) s_on_down();
}

void sim_press_up(void)
{
    if (!s_pressed) return;
    s_pressed = false;
    if (s_on_up) s_on_up();
}

static int event_watch(void *ud, SDL_Event *e)
{
    LV_UNUSED(ud);
    switch (e->type) {
    case SDL_QUIT:
        sim_quit = true;
        break;
    case SDL_MOUSEWHEEL:                       /* wheel = encoder */
        if (s_on_enc && e->wheel.y) s_on_enc(e->wheel.y > 0 ? 1 : -1);
        break;
    case SDL_KEYDOWN:
        if (e->key.repeat) break;
        switch (e->key.keysym.sym) {
        case SDLK_UP:   if (s_on_enc) s_on_enc(1);  break;
        case SDLK_DOWN: if (s_on_enc) s_on_enc(-1); break;
        case SDLK_SPACE:                       /* knob button down */
            s_pressed = true; s_hold_fired = false; s_press_t0 = sim_time_ms();
            if (s_on_down) s_on_down();
            break;
        case SDLK_ESCAPE:
        case SDLK_q:
            sim_quit = true;
            break;
        }
        break;
    case SDL_KEYUP:
        if (e->key.keysym.sym == SDLK_SPACE && s_pressed) {
            s_pressed = false;
            if (s_on_up) s_on_up();
            if (!s_hold_fired && s_on_btn) s_on_btn();   /* short release = click */
        }
        break;
    }
    return 0;
}

/* The headless panel. RENDER_MODE_DIRECT, so LVGL draws straight into this and
 * repaints ONLY the invalidated areas — every other pixel keeps whatever was there
 * before, exactly like the real panel's GRAM. That makes this buffer, and nothing
 * else, the thing that can prove an invalidation is complete: sim_shot() takes an
 * lv_snapshot, which re-renders the whole tree from scratch and therefore cannot
 * see a stale pixel by construction. Use sim_shot_fb() before and after a forced full redraw to compare. */
static uint16_t s_fb[SERIN_DISP * SERIN_DISP];

const uint16_t *sim_fb(void) { return sim_headless ? s_fb : NULL; }

/* Count pixels submitted through the display flush callback. This measures
 * dirty regions, not elapsed time or hardware performance: it can distinguish
 * an animation invalidating a rim from one invalidating the whole screen. */
static uint64_t s_dirty_px;
static uint32_t s_dirty_flushes;

uint64_t sim_dirty_px_take(uint32_t *flushes)
{
    uint64_t n = s_dirty_px;
    if (flushes) *flushes = s_dirty_flushes;
    s_dirty_px = 0; s_dirty_flushes = 0;
    return n;
}

static void flush_dummy(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    LV_UNUSED(px);
    if (a) {
        s_dirty_px += (uint64_t)(a->x2 - a->x1 + 1) * (uint64_t)(a->y2 - a->y1 + 1);
        s_dirty_flushes++;
    }
    lv_display_flush_ready(d);
}

/* ---- synthetic touch (headless `tap x y` / `swipe x0 y0 x1 y1 ms`) ----- *
 * Two-poll shape for taps: LVGL must observe PRESSED on one read and
 * RELEASED on a later one to synthesize PRESSED->CLICKED, so a tap spans two
 * read periods (~66 ms at the default 33 ms poll) — scripts must `wait`
 * after `tap`. A swipe owns the pointer instead: the point interpolates from
 * (x0,y0) to (x1,y1) over `ms` of sim time (PRESSED each poll), then
 * releases at the endpoint — sim_cmd blocks for the whole playback. */
static volatile int      s_tap_x, s_tap_y;
static volatile int      s_tap_phase;   /* 0 idle, 1 press pending, 2 held */
static volatile int      s_tap_hold;    /* sustained hold: stays PRESSED until touchup */
static volatile int      s_sw_x0, s_sw_y0, s_sw_x1, s_sw_y1;
static volatile uint32_t s_sw_ms;
static uint32_t          s_sw_t0;       /* tick of the swipe's first poll  */
static volatile int      s_sw_phase;    /* 0 idle, 1 armed, 2 playing      */

static void tap_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);
    if (s_sw_phase) {                       /* swipe playback owns the pointer */
        uint32_t now = sim_time_ms();
        if (s_sw_phase == 1) { s_sw_t0 = now; s_sw_phase = 2; }
        uint32_t el = now - s_sw_t0;
        if (el >= s_sw_ms) {                /* path done -> release at the end */
            data->point.x = s_sw_x1;
            data->point.y = s_sw_y1;
            data->state   = LV_INDEV_STATE_RELEASED;
            s_sw_phase = 0;
        } else {
            data->point.x = s_sw_x0 + (int)((int64_t)(s_sw_x1 - s_sw_x0) * el / s_sw_ms);
            data->point.y = s_sw_y0 + (int)((int64_t)(s_sw_y1 - s_sw_y0) * el / s_sw_ms);
            data->state   = LV_INDEV_STATE_PRESSED;
        }
        return;
    }
    if (s_tap_hold) {                       /* sustained hold: PRESSED every poll until touchup */
        data->point.x = s_tap_x;
        data->point.y = s_tap_y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else if (s_tap_phase) {
        data->point.x = s_tap_x;
        data->point.y = s_tap_y;
        data->state   = LV_INDEV_STATE_PRESSED;
        if (s_tap_phase == 1) s_tap_phase = 2;   /* one poll pressed ... */
        else                  s_tap_phase = 0;   /* ... then release next poll */
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void sim_tap(int x, int y)
{
    s_tap_x = x; s_tap_y = y; s_tap_phase = 1;
}

void sim_swipe(int x0, int y0, int x1, int y1, int ms)
{
    s_sw_x0 = x0; s_sw_y0 = y0; s_sw_x1 = x1; s_sw_y1 = y1;
    s_sw_ms = (uint32_t)(ms > 0 ? ms : 1);
    s_sw_phase = 1;
}

/* Sustained synthetic touch-hold: PRESSED at (x,y) every poll until sim_touch_up.
 * Non-blocking (unlike sim_swipe), so a script can `wait` and sample `info`
 * mid-hold — needed to observe the Settings hold-to-confirm fill arc. */
void sim_touch_hold(int x, int y) { s_tap_x = x; s_tap_y = y; s_tap_hold = 1; }
void sim_touch_up(void)           { s_tap_hold = 0; }

lv_display_t *bsp_display_start(void)
{
    if (sim_headless) {
        s_disp = lv_display_create(SERIN_DISP, SERIN_DISP);
        lv_display_set_flush_cb(s_disp, flush_dummy);
        lv_display_set_buffers(s_disp, s_fb, NULL, sizeof s_fb,
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_indev_t *tp = lv_indev_create();      /* synthetic touch for `tap` */
        lv_indev_set_type(tp, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(tp, tap_read_cb);
        lv_indev_set_display(tp, s_disp);
        /* one hold threshold for knob AND touch (interaction grammar) */
        lv_indev_set_long_press_time(tp, SERIN_HOLD_MS);
    } else {
#ifdef __EMSCRIPTEN__
        /* Only keys typed while the canvas has focus reach SDL — otherwise
         * Space in the page's text inputs would press the knob. */
        SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
        s_disp = lv_sdl_window_create(SERIN_DISP, SERIN_DISP);
        lv_indev_t *mi = lv_sdl_mouse_create();  /* mouse = touch */
        lv_indev_set_long_press_time(mi, SERIN_HOLD_MS);
    }
    return s_disp;
}

void bsp_backlight_set(int pct)
{
    printf("backlight: %d%%\n", pct);
    if (s_disp && !sim_headless) {
        char t[48];
        snprintf(t, sizeof t, "Serin sim — backlight %d%%", pct);
        lv_sdl_window_set_title(s_disp, t);
    }
}

/* Idle-off stub (ui_set_on_display_sleep, mirrors bsp_backlight_set above):
 * records the sleep call itself, rather than trusting ui.c's own s_disp_asleep,
 * so `info disp_sleep=` proves the callback plumbing actually fired. */
static bool s_disp_asleep;
void bsp_display_sleep(bool sleep)
{
    printf("display sleep: %d\n", sleep);
    s_disp_asleep = sleep;
}
bool sim_disp_asleep(void) { return s_disp_asleep; }

void bsp_input_start(void (*on_encoder)(int), void (*on_button)(void),
                     void (*on_button_hold)(void),
                     void (*on_press_down)(void), void (*on_press_up)(void))
{
    s_on_enc  = on_encoder;
    s_on_btn  = on_button;
    s_on_hold = on_button_hold;
    s_on_down = on_press_down;
    s_on_up   = on_press_up;
    /* The hold timer runs in EVERY mode: interactive holds arrive via SDL SPACE,
     * headless holds via sim_press_down/up — both set s_pressed so a 700 ms hold
     * fires s_on_hold (ui_button_long) faithfully. */
    lv_timer_create(hold_timer_cb, 20, NULL);
    if (!sim_headless) SDL_AddEventWatch(event_watch, NULL);
}
