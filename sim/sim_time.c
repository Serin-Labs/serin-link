// SPDX-License-Identifier: GPL-3.0-only
#include "sim_time.h"
#include "lvgl.h"
#include <SDL2/SDL.h>

static bool     s_virtual;
static uint32_t s_virt_ms;

void sim_time_init(bool virtual_time) { s_virtual = virtual_time; }

uint32_t sim_time_ms(void) { return s_virtual ? s_virt_ms : SDL_GetTicks(); }

void sim_time_wait(uint32_t ms)
{
    if (s_virtual) {
        uint32_t end = s_virt_ms + ms;
        while (s_virt_ms < end) { s_virt_ms += 5; lv_timer_handler(); }
    } else {
        uint32_t end = SDL_GetTicks() + ms;
        while (SDL_GetTicks() < end) { lv_timer_handler(); SDL_Delay(5); }
    }
}
