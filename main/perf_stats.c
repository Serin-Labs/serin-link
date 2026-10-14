/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "sdkconfig.h"
#if CONFIG_SERIN_PERF_STATS

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "perf_stats.h"
#include "perf_window.h"
#include "ui.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "perf_stats";

static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static perf_window_t s_window;
static int64_t s_window_start;

static void disp_evt(lv_event_t *e)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_stats_mux);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_RENDER_START: perf_window_start(&s_window, PERF_RENDER, now); break;
    case LV_EVENT_RENDER_READY: perf_window_end(&s_window, PERF_RENDER, now); break;
    case LV_EVENT_FLUSH_START: {
        perf_window_start(&s_window, PERF_FLUSH_CALL, now);
#if CONFIG_SERIN_BOARD_VIEWE_15
        const lv_area_t *area = lv_event_get_param(e);
        perf_window_start_transfer(&s_window, now,
                                   area ? (uint32_t)lv_area_get_size(area) * 2 : 0);
#endif
        break;
    }
    case LV_EVENT_FLUSH_FINISH: perf_window_end(&s_window, PERF_FLUSH_CALL, now); break;
    case LV_EVENT_FLUSH_WAIT_START: perf_window_start(&s_window, PERF_FLUSH_WAIT, now); break;
    case LV_EVENT_FLUSH_WAIT_FINISH: perf_window_end(&s_window, PERF_FLUSH_WAIT, now); break;
    default: break;
    }
    portEXIT_CRITICAL(&s_stats_mux);
}

void perf_stats_transfer_done(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_stats_mux);
    perf_window_end(&s_window, PERF_TRANSFER, now);
    portEXIT_CRITICAL_ISR(&s_stats_mux);
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

typedef struct {
    perf_sample_t samples[PERF_METRIC_COUNT];
    uint64_t bytes;
    int64_t elapsed;
} timing_snapshot_t;

static void window_take(timing_snapshot_t *snapshot)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_stats_mux);
    snapshot->elapsed = now - s_window_start;
    s_window_start = now;
    perf_window_take(&s_window, snapshot->samples, &snapshot->bytes);
    portEXIT_CRITICAL(&s_stats_mux);
}

static void window_print(const timing_snapshot_t *snapshot)
{
    ESP_LOGI(TAG, "window=%" PRId64 "ms qspi-completed-bytes=%" PRIu64,
             snapshot->elapsed / 1000, snapshot->bytes);
    static const char *names[] = { "render", "flush-call", "flush-wait", "qspi-complete" };
    for (unsigned i = 0; i < PERF_METRIC_COUNT; i++) {
        const perf_sample_t *s = &snapshot->samples[i];
        ESP_LOGI(TAG, "%s n=%u avg=%" PRIu64 "us max=%uus", names[i],
                 (unsigned)s->count, s->count ? s->total_us / s->count : 0,
                 (unsigned)s->max_us);
    }
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
static TaskStatus_t *s_previous_tasks;
static UBaseType_t s_previous_count;
static uint32_t s_previous_total;

typedef struct {
    TaskStatus_t *tasks, *previous_tasks;
    UBaseType_t count, previous_count;
    uint32_t elapsed;
    enum { CPU_CAPTURE_OK, CPU_CAPTURE_NO_MEMORY, CPU_CAPTURE_TASKS_CHANGED } error;
} cpu_snapshot_t;

static void cpu_take(cpu_snapshot_t *snapshot)
{
    *snapshot = (cpu_snapshot_t){
        .previous_tasks = s_previous_tasks,
        .previous_count = s_previous_count,
    };
    /* A failed capture must invalidate the old baseline: the timing window
     * still advances, so retaining it would silently span multiple intervals. */
    s_previous_tasks = NULL;
    s_previous_count = 0;
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
    TaskStatus_t *ts = heap_caps_malloc(n * sizeof *ts, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ts) { snapshot->error = CPU_CAPTURE_NO_MEMORY; return; }
    uint32_t total = 0;
    n = uxTaskGetSystemState(ts, n, &total);
    if (!n) { free(ts); snapshot->error = CPU_CAPTURE_TASKS_CHANGED; return; }
    snapshot->tasks = ts;
    snapshot->count = n;
    snapshot->elapsed = total - s_previous_total;
    s_previous_tasks = ts;
    s_previous_count = n;
    s_previous_total = total;
}

static void cpu_finish(cpu_snapshot_t *snapshot, bool print)
{
    if (snapshot->error == CPU_CAPTURE_NO_MEMORY) ESP_LOGW(TAG, "task table: no PSRAM");
    else if (snapshot->error == CPU_CAPTURE_TASKS_CHANGED)
        ESP_LOGW(TAG, "task table changed during snapshot; try again");
    else if (print && snapshot->previous_tasks && snapshot->elapsed) {
        printf("-- live tasks: CPU %% of one core over last interval; stack bytes free --\n");
        printf("%-16s %4s %3s %5s %7s\n", "name", "prio", "core", "cpu%", "stack");
        for (UBaseType_t i = 0; i < snapshot->count; i++) {
            const TaskStatus_t *task = &snapshot->tasks[i];
            uint32_t before = 0; /* A newly created task started inside this interval. */
            for (UBaseType_t j = 0; j < snapshot->previous_count; j++) {
                const TaskStatus_t *previous = &snapshot->previous_tasks[j];
                if (previous->xHandle == task->xHandle && previous->xTaskNumber == task->xTaskNumber) {
                    before = previous->ulRunTimeCounter;
                    break;
                }
            }
            unsigned pct10 = perf_cpu_tenths(task->ulRunTimeCounter, before, snapshot->elapsed);
#if CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
            int core = (int)task->xCoreID;
#else
            int core = -1;
#endif
            printf("%-16s %4u %3d %3u.%u %7u\n", task->pcTaskName,
                   (unsigned)task->uxCurrentPriority, core > 1 ? -1 : core,
                   pct10 / 10, pct10 % 10, (unsigned)task->usStackHighWaterMark);
        }
    }
    free(snapshot->previous_tasks);
}
#else
typedef struct { int unused; } cpu_snapshot_t;
static void cpu_take(cpu_snapshot_t *snapshot) { (void)snapshot; }
static void cpu_finish(cpu_snapshot_t *snapshot, bool print) { (void)snapshot; (void)print; }
#endif

static void reset_window(void)
{
    timing_snapshot_t timing;
    cpu_snapshot_t cpu;
    window_take(&timing);
    cpu_take(&cpu);
    cpu_finish(&cpu, false);
}

static void dump_and_reset(void)
{
    timing_snapshot_t timing;
    cpu_snapshot_t cpu;
    window_take(&timing);
    cpu_take(&cpu);
    /* Close both intervals before any logging, including snapshot errors. */
    window_print(&timing);
    heap_line("now");
    cpu_finish(&cpu, true);
}

static int cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    reset_window();
    return 0;
}

static int cmd_dump(int argc, char **argv)
{
    (void)argc; (void)argv;
    dump_and_reset();
    return 0;
}

static int cmd_glide(int argc, char **argv)
{
    (void)argc; (void)argv;
    reset_window();
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
    reset_window();
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        if (lvgl_port_lock(0)) { ui_show_screen(seq[i]); lvgl_port_unlock(); }
        vTaskDelay(pdMS_TO_TICKS(600));
    }
    dump_and_reset();
    return 0;
}

static int cmd_pm(int argc, char **argv)
{
    (void)argc; (void)argv;
#if CONFIG_PM_ENABLE
    return esp_pm_dump_locks(stdout);
#else
    puts("CPU power management disabled");
    return 0;
#endif
}

static bool try_register_console(void)
{
    const esp_console_cmd_t cmds[] = {
        { .command = "perf-reset", .help = "start a new timing and CPU measurement window", .func = cmd_reset },
        { .command = "perf-dump",  .help = "print+reset timing, heap, interval CPU share", .func = cmd_dump },
        { .command = "perf-glide", .help = "scripted +-10 detent setpoint ramp",          .func = cmd_glide },
        { .command = "perf-nav",   .help = "home->menu->settings->home cycle",            .func = cmd_nav },
        { .command = "perf-pm",    .help = "print CPU/peripheral power-management locks", .func = cmd_pm },
    };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++)
        if (esp_console_cmd_register(&cmds[i]) != ESP_OK) return false;
    return true;
}

static void console_register_task(void *arg)
{
    (void)arg;
    while (!try_register_console()) vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelete(NULL);
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
    lv_display_add_event_cb(disp, disp_evt, LV_EVENT_FLUSH_WAIT_START, NULL);
    lv_display_add_event_cb(disp, disp_evt, LV_EVENT_FLUSH_WAIT_FINISH, NULL);
    lvgl_port_unlock();
    reset_window();
    heap_line("post-display-init");
    if (xTaskCreate(console_register_task, "perf_console", 4096, NULL, 2, NULL) != pdPASS)
        ESP_LOGE(TAG, "perf console task creation failed");
    ESP_LOGI(TAG, "perf stats armed (explicit windows; perf-reset/perf-dump)");
}

#endif
