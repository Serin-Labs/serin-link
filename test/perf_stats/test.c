/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 * Drive the actual perf console commands. Hardware and RTOS boundaries are
 * replaced; reporting consumes fake time and worker CPU to expose late captures.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "test_platform.h"
#include "perf_stats.h"
#include "ui.h"

static int64_t s_now = 100000;
static uint32_t s_worker_runtime = 10000;
static char s_output[8192];
static size_t s_output_len;
static esp_console_cmd_t s_commands[8];
static unsigned s_command_count;
static bool s_alloc_fail, s_task_table_fail;

static int output(const char *fmt, va_list args)
{
    int n = vsnprintf(s_output + s_output_len, sizeof s_output - s_output_len, fmt, args);
    assert(n >= 0 && (size_t)n < sizeof s_output - s_output_len);
    s_output_len += (size_t)n;
    s_now += 100;
    s_worker_runtime += 100;
    return n;
}
int test_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = output(fmt, args);
    va_end(args);
    return n;
}
int test_puts(const char *s) { return test_printf("%s\n", s); }
void test_log(const char *level, const char *tag, const char *fmt, ...)
{
    (void)level; (void)tag;
    va_list args;
    va_start(args, fmt);
    output(fmt, args);
    va_end(args);
    s_output[s_output_len++] = '\n';
    s_output[s_output_len] = 0;
}
int64_t esp_timer_get_time(void) { return s_now; }
UBaseType_t uxTaskGetNumberOfTasks(void) { return 1; }
UBaseType_t uxTaskGetSystemState(TaskStatus_t *tasks, UBaseType_t count, uint32_t *total)
{
    assert(count >= 1);
    if (s_task_table_fail) return 0;
    tasks[0] = (TaskStatus_t){
        .xHandle = (void *)1, .pcTaskName = "worker", .xTaskNumber = 1,
        .uxCurrentPriority = 2, .xCoreID = 0, .usStackHighWaterMark = 2048,
        .ulRunTimeCounter = s_worker_runtime,
    };
    *total = (uint32_t)s_now;
    return 1;
}
void *heap_caps_malloc(size_t n, unsigned caps)
{ (void)caps; return s_alloc_fail ? NULL : malloc(n); }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 100000; }
size_t heap_caps_get_largest_free_block(unsigned caps) { (void)caps; return 50000; }
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *cmd)
{
    assert(s_command_count < sizeof s_commands / sizeof s_commands[0]);
    s_commands[s_command_count++] = *cmd;
    return ESP_OK;
}
int xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle)
{
    (void)name; (void)stack; (void)priority; (void)handle;
    task(arg);
    return pdPASS;
}
void vTaskDelete(void *task) { (void)task; }
void vTaskDelay(unsigned ms) { s_now += ms * 1000; }
bool lvgl_port_lock(uint32_t ms) { (void)ms; return true; }
void lvgl_port_unlock(void) {}
unsigned lv_event_get_code(lv_event_t *event) { return event->code; }
void *lv_event_get_param(lv_event_t *event) { return event->param; }
int lv_area_get_size(const lv_area_t *a) { return (a->x2 - a->x1 + 1) * (a->y2 - a->y1 + 1); }
void lv_display_add_event_cb(lv_display_t *disp, void (*cb)(lv_event_t *), unsigned code, void *ctx)
{ (void)disp; (void)cb; (void)code; (void)ctx; }
void ui_encoder(int delta) { (void)delta; }
void ui_show_screen(screen_id_t screen) { (void)screen; }

static void command(const char *name)
{
    for (unsigned i = 0; i < s_command_count; i++) {
        if (!strcmp(s_commands[i].command, name)) {
            assert(s_commands[i].func(0, NULL) == 0);
            return;
        }
    }
    assert(!"performance command missing");
}
static void clear_output(void) { s_output_len = 0; s_output[0] = 0; }

int main(void)
{
    lv_display_t disp = {0};
    perf_stats_init(&disp);
    command("perf-reset");
    clear_output();
    s_now += 1000;
    s_worker_runtime += 100;
    command("perf-dump");
    const char *worker = strstr(s_output, "worker");
    assert(worker);
    char name[32];
    unsigned priority, whole, tenth, stack;
    int core;
    assert(sscanf(worker, "%31s %u %d %u.%u %u", name, &priority, &core,
                  &whole, &tenth, &stack) == 6);
    if (whole != 10 || tenth != 0) {
        fprintf(stderr, "CPU interval included reporting: expected 10.0%%, got %u.%u%%\n", whole, tenth);
        return 1;
    }
    assert(strstr(s_output, "window=1ms"));
    /* A failed CPU capture cannot leave a baseline spanning multiple timing
     * windows. The first successful capture re-arms CPU sampling without a
     * stale percentage; the following capture can report normally. */
    for (unsigned failure = 0; failure < 2; failure++) {
        clear_output();
        s_alloc_fail = failure == 0;
        s_task_table_fail = failure == 1;
        s_now += 1000;
        s_worker_runtime += 100;
        command("perf-dump");
        assert(strstr(s_output, failure == 0 ? "no PSRAM" : "task table changed"));
        assert(!strstr(s_output, "worker"));
        s_alloc_fail = s_task_table_fail = false;
        clear_output();
        s_now += 1000;
        s_worker_runtime += 100;
        command("perf-dump");
        assert(!strstr(s_output, "worker"));
        clear_output();
        command("perf-reset");
        s_now += 1000;
        s_worker_runtime += 100;
        command("perf-dump");
        worker = strstr(s_output, "worker");
        assert(worker);
        assert(sscanf(worker, "%31s %u %d %u.%u %u", name, &priority, &core,
                      &whole, &tenth, &stack) == 6);
        assert(whole == 10 && tenth == 0);
    }
    fprintf(stderr, "perf_stats: snapshot before reporting OK\n");
    return 0;
}
