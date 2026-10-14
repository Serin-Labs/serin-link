/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CONFIG_SERIN_PERF_STATS 1
#define CONFIG_SERIN_BOARD_VIEWE_15 1
#define CONFIG_PM_ENABLE 0
#define CONFIG_FREERTOS_USE_TRACE_FACILITY 1
#define CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS 1
#define CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERROR_CHECK(err) assert((err) == ESP_OK)
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define portENTER_CRITICAL_ISR(mux) ((void)(mux))
#define portEXIT_CRITICAL_ISR(mux) ((void)(mux))
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef unsigned UBaseType_t;
typedef struct {
    void *xHandle;
    const char *pcTaskName;
    unsigned xTaskNumber, uxCurrentPriority, xCoreID, usStackHighWaterMark;
    uint32_t ulRunTimeCounter;
} TaskStatus_t;
UBaseType_t uxTaskGetNumberOfTasks(void);
UBaseType_t uxTaskGetSystemState(TaskStatus_t *, UBaseType_t, uint32_t *);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(unsigned);
void vTaskDelete(void *);
int64_t esp_timer_get_time(void);
void *heap_caps_malloc(size_t, unsigned);
size_t heap_caps_get_free_size(unsigned);
size_t heap_caps_get_largest_free_block(unsigned);
void test_log(const char *, const char *, const char *, ...);
#define ESP_LOGI(tag, ...) test_log("I", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) test_log("W", tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) test_log("E", tag, __VA_ARGS__)

typedef struct { int unused; } lv_display_t;
typedef struct { int x1, y1, x2, y2; } lv_area_t;
typedef struct { unsigned code; void *param; } lv_event_t;
enum {
    LV_EVENT_RENDER_START, LV_EVENT_RENDER_READY, LV_EVENT_FLUSH_START,
    LV_EVENT_FLUSH_FINISH, LV_EVENT_FLUSH_WAIT_START, LV_EVENT_FLUSH_WAIT_FINISH
};
unsigned lv_event_get_code(lv_event_t *);
void *lv_event_get_param(lv_event_t *);
int lv_area_get_size(const lv_area_t *);
void lv_display_add_event_cb(lv_display_t *, void (*)(lv_event_t *), unsigned, void *);
bool lvgl_port_lock(uint32_t);
void lvgl_port_unlock(void);
typedef struct {
    const char *command, *help;
    int (*func)(int, char **);
} esp_console_cmd_t;
esp_err_t esp_console_cmd_register(const esp_console_cmd_t *);
