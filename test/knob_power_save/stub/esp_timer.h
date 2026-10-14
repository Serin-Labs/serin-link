/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct test_timer *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *);
    void *arg;
    int dispatch_method;
    const char *name;
} esp_timer_create_args_t;
#define ESP_TIMER_TASK 0
esp_err_t esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t);
esp_err_t esp_timer_stop(esp_timer_handle_t);
esp_err_t esp_timer_delete(esp_timer_handle_t);
