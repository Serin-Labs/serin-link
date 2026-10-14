/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 * Exercise our callback ownership and completion ordering. Only the hardware,
 * RTOS, and LVGL API boundary is replaced; waits use real pthread conditions.
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "display_runtime.h"
#include "esp_lvgl_port.h"
#include "freertos/semphr.h"

struct test_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool token, waiting;
};
static struct test_semaphore *s_sem;
static bool s_alloc_fail, s_register_fail;
static esp_lcd_panel_io_callbacks_t s_callbacks;
static void *s_ctx;
static int s_locked, s_stop_attempts, s_delays, s_wakes;
static int s_startup_pending = 2;
static esp_err_t s_stop_error = ESP_OK;
static bool s_periodic = true, s_timers = true;
static int64_t s_now_us = 8999500;
static uint32_t (*s_tick)(void);
static unsigned s_ready;

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    if (s_alloc_fail) return NULL;
    s_sem = calloc(1, sizeof *s_sem);
    assert(s_sem);
    assert(!pthread_mutex_init(&s_sem->mutex, NULL));
    assert(!pthread_cond_init(&s_sem->changed, NULL));
    return s_sem;
}

void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    pthread_cond_destroy(&sem->changed);
    pthread_mutex_destroy(&sem->mutex);
    free(sem);
    s_sem = NULL;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    assert(sem);
    pthread_mutex_lock(&sem->mutex);
    while (!sem->token && ticks == portMAX_DELAY) {
        sem->waiting = true;
        pthread_cond_broadcast(&sem->changed);
        pthread_cond_wait(&sem->changed, &sem->mutex);
    }
    bool got = sem->token;
    sem->token = false;
    sem->waiting = false;
    pthread_mutex_unlock(&sem->mutex);
    return got ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t sem, BaseType_t *woken)
{
    assert(sem);
    pthread_mutex_lock(&sem->mutex);
    assert(!sem->token); /* One completion per transfer; no stale token. */
    sem->token = true;
    *woken = sem->waiting ? pdTRUE : pdFALSE;
    pthread_cond_broadcast(&sem->changed);
    pthread_mutex_unlock(&sem->mutex);
    return pdTRUE;
}

esp_err_t esp_lcd_panel_io_register_event_callbacks(
    esp_lcd_panel_io_handle_t io, const esp_lcd_panel_io_callbacks_t *cb, void *ctx)
{
    assert(io);
    if (s_register_fail) return ESP_FAIL;
    s_callbacks = *cb;
    s_ctx = ctx;
    return ESP_OK;
}

void lv_display_set_flush_wait_cb(lv_display_t *d, void (*cb)(lv_display_t *))
{
    d->wait_cb = cb;
}
void lv_display_flush_ready(lv_display_t *disp) { assert(disp); s_ready++; }

bool lvgl_port_lock(uint32_t ms) { (void)ms; assert(!s_locked); s_locked++; return true; }
void lvgl_port_unlock(void) { assert(s_locked == 1); s_locked--; }
esp_err_t lvgl_port_stop(void)
{
    assert(s_locked);
    s_stop_attempts++;
    /* The port signals startup before creating/starting its tick timer. */
    s_timers = false;
    if (s_stop_error != ESP_OK) return s_stop_error;
    if (s_startup_pending-- > 0) return ESP_ERR_INVALID_STATE;
    s_periodic = false;
    return ESP_OK;
}
void vTaskDelay(TickType_t ticks)
{
    assert(!s_locked && ticks > 0);
    assert(s_timers); /* A failed stop must not leave the watchdog suspended. */
    s_delays++;
}
void lv_timer_enable(bool on) { assert(s_locked); s_timers = on; }
void lv_tick_set_cb(uint32_t (*cb)(void)) { assert(s_locked); s_tick = cb; }
uint32_t lv_tick_get(void) { return s_tick ? s_tick() : 123; }
int64_t esp_timer_get_time(void) { return s_now_us; }
esp_err_t lvgl_port_task_wake(int event, void *param)
{
    assert(event == LVGL_PORT_EVENT_USER && !param);
    s_wakes++;
    return ESP_OK;
}

static atomic_bool s_wait_returned;
static void *wait_thread(void *arg)
{
    lv_display_t *disp = arg;
    disp->wait_cb(disp);
    atomic_store(&s_wait_returned, true);
    return NULL;
}

int main(int argc, char **argv)
{
    alarm(5); /* A lost wake/completion fails instead of hanging CI. */
    if (argc > 1 && !strcmp(argv[1], "--startup-timeout")) {
        s_startup_pending = 1000;
        assert(display_runtime_start() == ESP_ERR_TIMEOUT);
        assert(s_stop_attempts == 100 && s_delays == 100);
        assert(s_timers && s_periodic && !s_locked && !s_tick && !s_wakes);
        puts("display_runtime: bounded startup timeout OK");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--stop-error")) {
        s_stop_error = ESP_FAIL;
        assert(display_runtime_start() == ESP_FAIL);
        assert(s_stop_attempts == 1 && !s_delays);
        assert(s_timers && s_periodic && !s_locked && !s_tick && !s_wakes);
        puts("display_runtime: tick-stop failure leaves LVGL timers enabled");
        return 0;
    }
    assert(display_runtime_start() == ESP_OK);
    assert(s_stop_attempts == 3 && s_delays == 2);
    assert(!s_periodic && s_timers && s_wakes == 1 && !s_locked);
    assert(lv_tick_get() == 123); /* Switching clocks does not jump UI time. */
    s_now_us += 1500;
    assert(lv_tick_get() == 125);
    s_now_us += 10000000;
    assert(lv_tick_get() == 10125); /* No tick callbacks during a long idle. */
    s_now_us = ((int64_t)8876 + UINT32_MAX + 20) * 1000;
    assert(lv_tick_get() == 19); /* LVGL's 32-bit millisecond wrap. */

    lv_display_t disp = {0};
    esp_lcd_panel_io_handle_t io = &disp;
    if (argc > 1) {
        assert(display_runtime_attach(&disp, io, false) == ESP_OK);
        assert(!disp.wait_cb && !s_sem);
        assert(!s_callbacks.on_color_trans_done(io, NULL, s_ctx));
        assert(s_ready == 1);
        puts("display_runtime: polling baseline OK");
        return 0;
    }
    assert(display_runtime_attach(NULL, io, true) == ESP_ERR_INVALID_ARG);
    assert(display_runtime_attach(&disp, NULL, true) == ESP_ERR_INVALID_ARG);
    s_alloc_fail = true;
    assert(display_runtime_attach(&disp, io, true) == ESP_ERR_NO_MEM);
    assert(!disp.wait_cb);
    s_alloc_fail = false;
    s_register_fail = true;
    assert(display_runtime_attach(&disp, io, true) == ESP_FAIL);
    assert(!disp.wait_cb && !s_sem);
    s_register_fail = false;
    assert(display_runtime_attach(&disp, io, true) == ESP_OK);
    assert(disp.wait_cb && s_callbacks.on_color_trans_done);
    assert(display_runtime_attach(&disp, io, true) == ESP_ERR_INVALID_STATE);

    /* DMA may finish before LVGL decides to wait. The completion must persist. */
    assert(!s_callbacks.on_color_trans_done(io, NULL, s_ctx));
    disp.wait_cb(&disp);
    assert(!s_sem->token);

    /* Each later transfer blocks until its own completion, even after an early
     * completion in the preceding transfer. Neither buffer may be reused early. */
    for (int i = 0; i < 100; i++) {
        atomic_store(&s_wait_returned, false);
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, wait_thread, &disp));
        pthread_mutex_lock(&s_sem->mutex);
        while (!s_sem->waiting) pthread_cond_wait(&s_sem->changed, &s_sem->mutex);
        assert(!atomic_load(&s_wait_returned));
        pthread_mutex_unlock(&s_sem->mutex);
        assert(s_callbacks.on_color_trans_done(io, NULL, s_ctx));
        assert(!pthread_join(thread, NULL));
        assert(atomic_load(&s_wait_returned) && !s_sem->token);
    }
    vSemaphoreDelete(s_sem);
    assert(s_ready == 0); /* LVGL owns flushing in the blocking path. */
    puts("display_runtime: OK");
}
