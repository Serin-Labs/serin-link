/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "sdkconfig.h"
#if CONFIG_SERIN_PERF_STATS

#include <stdatomic.h>
#include <stdio.h>
#include "perf_stats.h"
#include "ui.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "perf_stats";

static int64_t  s_render_t0, s_flush_t0;
static uint32_t s_renders, s_render_us, s_render_max;
static uint32_t s_flushes, s_flush_us, s_flush_max;

static void disp_evt(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
    case LV_EVENT_RENDER_START: s_render_t0 = now; break;
    case LV_EVENT_RENDER_READY: {
        uint32_t us = (uint32_t)(now - s_render_t0);
        s_renders++; s_render_us += us;
        if (us > s_render_max) s_render_max = us;
        break;
    }
    case LV_EVENT_FLUSH_START:  s_flush_t0 = now; break;
    case LV_EVENT_FLUSH_FINISH: {
        uint32_t us = (uint32_t)(now - s_flush_t0);
        s_flushes++; s_flush_us += us;
        if (us > s_flush_max) s_flush_max = us;
        break;
    }
    default: break;
    }
}

static void heap_line(const char *when)
{
    ESP_LOGI(TAG, "%s heap: int free=%u largest=%u | dma free=%u largest=%u | psram free=%u",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void dump_and_reset(void)
{
    uint32_t rn = s_renders, ru = s_render_us, rm = s_render_max;
    uint32_t fn = s_flushes, fu = s_flush_us, fm = s_flush_max;
    s_renders = s_render_us = s_render_max = 0;
    s_flushes = s_flush_us = s_flush_max = 0;
    ESP_LOGI(TAG, "renders=%u avg=%uus max=%uus | flushes=%u avg=%uus max=%uus",
             (unsigned)rn, (unsigned)(rn ? ru / rn : 0), (unsigned)rm,
             (unsigned)fn, (unsigned)(fn ? fu / fn : 0), (unsigned)fm);
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static void cpu_share_dump(void)
{
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *ts = heap_caps_malloc(n * sizeof *ts, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ts) { ESP_LOGW(TAG, "task table: no PSRAM"); return; }
    uint32_t total = 0;
    n = uxTaskGetSystemState(ts, n, &total);
    if (!total) total = 1;
    printf("-- tasks: cpu%% since boot, stack high-water (bytes free) --\n");
    printf("%-16s %4s %3s %5s %7s\n", "name", "prio", "core", "cpu%", "stack");
    for (UBaseType_t i = 0; i < n; i++) {
        unsigned pct10 = (unsigned)((uint64_t)ts[i].ulRunTimeCounter * 1000 / total);
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
        int core = (int)ts[i].xCoreID;
#else
        int core = -1;
#endif
        printf("%-16s %4u %3d %3u.%u %7u\n", ts[i].pcTaskName,
               (unsigned)ts[i].uxCurrentPriority, core > 1 ? -1 : core,
               pct10 / 10, pct10 % 10, (unsigned)ts[i].usStackHighWaterMark);
    }
    free(ts);
}
#else
static void cpu_share_dump(void) {}
#endif

static int cmd_dump(int argc, char **argv)
{
    (void)argc; (void)argv;
    dump_and_reset();
    heap_line("now");
    cpu_share_dump();
    return 0;
}

static int cmd_glide(int argc, char **argv)
{
    (void)argc; (void)argv;
    dump_and_reset();
    for (int dir = +1; dir >= -1; dir -= 2) {
        for (int i = 0; i < 10; i++) {
            if (lvgl_port_lock(0)) { ui_encoder(dir); lvgl_port_unlock(); }
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(400));
    dump_and_reset();
    return 0;
}

static int cmd_nav(int argc, char **argv)
{
    (void)argc; (void)argv;
    static const screen_id_t seq[] = { SCREEN_MENU, SCREEN_SETTINGS, SCREEN_THERMOSTAT };
    dump_and_reset();
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        if (lvgl_port_lock(0)) { ui_show_screen(seq[i]); lvgl_port_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    dump_and_reset();
    return 0;
}

static bool try_register_console(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "perf-dump",  .help = "print+reset perf counters, heap, CPU share", .func = cmd_dump },
        { .command = "perf-glide", .help = "scripted +-10 detent setpoint ramp",          .func = cmd_glide },
        { .command = "perf-nav",   .help = "home->menu->settings->home cycle",            .func = cmd_nav },
    };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++)
        if (esp_console_cmd_register(&cmds[i]) != ESP_OK) return false;
    return true;
}

static void dump_task(void *arg)
{
    (void)arg;
    bool console_ok = false;
    int tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!console_ok) console_ok = try_register_console();
        dump_and_reset();
        if (++tick % 3 == 0) {
            heap_line("now");
            cpu_share_dump();
        }
    }
}

void perf_stats_init(lv_display_t *disp)
{
    /* taskLVGL is already rendering by now and walks this same event list, so
     * take the mutex rather than relying on the caller to hold it. */
    ESP_ERROR_CHECK(lvgl_port_lock(0) ? ESP_OK : ESP_ERR_TIMEOUT);
    lv_display_add_event_cb(disp, disp_evt, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, disp_evt, LV_EVENT_RENDER_READY, NULL);
    lv_display_add_event_cb(disp, disp_evt, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, disp_evt, LV_EVENT_FLUSH_FINISH, NULL);
    lvgl_port_unlock();
    heap_line("post-display-init");
    xTaskCreate(dump_task, "perf_dump", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "perf stats armed (5s window)");
}

#endif
