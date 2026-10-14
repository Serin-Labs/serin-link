/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "display_runtime.h"
#include <stddef.h>
#include "sdkconfig.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "perf_stats.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if CONFIG_SERIN_LVGL_MONOTONIC_TICK
static uint32_t s_tick_offset;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000) - s_tick_offset;
}
#endif

esp_err_t display_runtime_start(void)
{
#if CONFIG_SERIN_LVGL_MONOTONIC_TICK
    /* In port 2.8.0, init signals its caller before creating the tick timer.
     * Retry that short startup race without holding the LVGL mutex while idle.
     * stop() disables all LVGL timers: always re-enable them, including on an
     * unsuccessful stop, so UI timing and the LVGL watchdog remain live. */
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        if (!lvgl_port_lock(0)) return ESP_ERR_TIMEOUT;
        esp_err_t err = lvgl_port_stop();
        if (err == ESP_OK) {
            s_tick_offset = (uint32_t)(esp_timer_get_time() / 1000) - lv_tick_get();
            lv_tick_set_cb(tick_ms);
        }
        lv_timer_enable(true);
        lvgl_port_unlock();
        if (err == ESP_OK) return lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);
        if (err != ESP_ERR_INVALID_STATE) return err;
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
#else
    return ESP_OK;
#endif
}

static SemaphoreHandle_t s_done;
static bool s_attached;

static bool transfer_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *event, void *ctx)
{
    (void)io; (void)event;
    perf_stats_transfer_done();
    if (!s_done) {
        lv_display_flush_ready((lv_display_t *)ctx);
        return false;
    }
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void flush_wait(lv_display_t *disp)
{
    (void)disp;
    xSemaphoreTake(s_done, portMAX_DELAY);
}

esp_err_t display_runtime_attach(lv_display_t *disp, esp_lcd_panel_io_handle_t io,
                                 bool blocking)
{
    if (!disp || !io) return ESP_ERR_INVALID_ARG;
    if (s_attached) return ESP_ERR_INVALID_STATE;
    if (blocking) {
        s_done = xSemaphoreCreateBinary();
        if (!s_done) return ESP_ERR_NO_MEM;
    }
    const esp_lcd_panel_io_callbacks_t callbacks = { .on_color_trans_done = transfer_done };
    esp_err_t err = esp_lcd_panel_io_register_event_callbacks(io, &callbacks, disp);
    if (err != ESP_OK) {
        if (s_done) vSemaphoreDelete(s_done);
        s_done = NULL;
        return err;
    }
    /* LVGL clears flushing after this wait returns. The ISR must NOT also call
     * flush_ready(): that can race the next transfer or leave an unconsumed
     * semaphore token after early completion. Each flush consumes one token. */
    if (blocking) lv_display_set_flush_wait_cb(disp, flush_wait);
    s_attached = true;
    return ESP_OK;
}
