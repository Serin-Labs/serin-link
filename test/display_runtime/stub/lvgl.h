#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct lv_display_t lv_display_t;
struct lv_display_t {
    void (*wait_cb)(lv_display_t *);
};
void lv_display_set_flush_wait_cb(lv_display_t *, void (*)(lv_display_t *));
void lv_display_flush_ready(lv_display_t *);
void lv_tick_set_cb(uint32_t (*)(void));
uint32_t lv_tick_get(void);
void lv_timer_enable(bool enabled);
