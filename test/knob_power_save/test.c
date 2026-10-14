/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 * Compile the actual pinned/generated driver. Hardware and timer boundaries
 * simulate interrupts delivered immediately when their GPIO is registered.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_timer.h"

static void *tracked_calloc(size_t n, size_t size);
static void tracked_free(void *ptr);
#define calloc tracked_calloc
#define free tracked_free
#include KNOB_DRIVER_SOURCE
#undef calloc
#undef free

struct test_timer {
    void (*callback)(void *);
    void *arg;
    bool running;
};
static struct test_timer s_timer;
static bool s_create_fail, s_inject_interrupts, s_sample_during_registration;
static unsigned s_allocations, s_registrations, s_bad_starts, s_gpio_resets, s_starts;
static unsigned s_left, s_right;
static uint8_t s_levels[64];
static bool s_interrupts[64], s_wake[64];
static gpio_int_type_t s_types[64];
static gpio_isr_t s_handlers[64];
static void *s_handler_args[64];

static void *tracked_calloc(size_t n, size_t size)
{
    void *p = calloc(n, size);
    if (p) s_allocations++;
    return p;
}
static void tracked_free(void *p)
{
    if (p) { assert(s_allocations); s_allocations--; }
    free(p);
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    if (s_create_fail) return ESP_ERR_NO_MEM;
    s_timer.callback = args->callback;
    s_timer.arg = args->arg;
    *out = &s_timer;
    return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
    assert(period == 3000);
    if (!timer) { s_bad_starts++; return ESP_ERR_INVALID_ARG; }
    if (timer->running) return ESP_ERR_INVALID_STATE;
    timer->running = true;
    s_starts++;
    return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t timer)
{
    if (!timer || !timer->running) return ESP_ERR_INVALID_STATE;
    timer->running = false;
    return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t timer)
{
    assert(timer == &s_timer && !timer->running);
    return ESP_OK;
}
esp_err_t knob_gpio_init(uint32_t gpio)
{
    s_levels[gpio] = 1;
    return ESP_OK;
}
esp_err_t knob_gpio_deinit(uint32_t gpio)
{
    (void)gpio;
    s_gpio_resets++;
    return ESP_OK;
}
uint8_t knob_gpio_get_key_level(void *gpio) { return s_levels[(uintptr_t)gpio]; }
esp_err_t knob_gpio_set_intr(uint32_t gpio, gpio_int_type_t type)
{
    s_types[gpio] = type;
    return ESP_OK;
}
esp_err_t knob_gpio_intr_control(uint32_t gpio, bool enabled)
{
    s_interrupts[gpio] = enabled;
    return ESP_OK;
}
esp_err_t knob_gpio_wake_up_control(uint32_t gpio, uint8_t level, bool enabled)
{
    (void)level;
    s_wake[gpio] = enabled;
    return ESP_OK;
}
esp_err_t knob_gpio_wake_up_init(uint32_t gpio, uint8_t level)
{
    return knob_gpio_wake_up_control(gpio, level, true);
}
esp_err_t knob_gpio_init_intr(uint32_t gpio, gpio_int_type_t type,
                             gpio_isr_t handler, void *arg)
{
    s_registrations++;
    s_types[gpio] = type;
    s_handlers[gpio] = handler;
    s_handler_args[gpio] = arg;
    s_interrupts[gpio] = true; /* IDF gpio_isr_handler_add enables the pin. */
    if (s_inject_interrupts) {
        /* A brief startup edge can return to its original level before the
         * debounce sampler reads it. Such an IRQ must not invent a detent. */
        handler(arg);
        /* The timer task may run before creation returns. It must see the
         * initialized knob, including KNOB_CHECK rather than calloc's zero. */
        if (s_sample_during_registration && s_timer.running)
            s_timer.callback(s_timer.arg);
    }
    return ESP_OK;
}
static void sample(unsigned n)
{
    for (unsigned i = 0; i < n && s_timer.running; i++) s_timer.callback(s_timer.arg);
}
static void level(uint32_t gpio, uint8_t value)
{
    s_levels[gpio] = value;
    if (s_interrupts[gpio] &&
        ((s_types[gpio] == GPIO_INTR_LOW_LEVEL && !value) ||
         (s_types[gpio] == GPIO_INTR_HIGH_LEVEL && value))) {
        s_handlers[gpio](s_handler_args[gpio]);
    }
    sample(3);
}
static void on_left(void *knob, void *arg) { (void)knob; (void)arg; s_left++; }
static void on_right(void *knob, void *arg) { (void)knob; (void)arg; s_right++; }

int main(int argc, char **argv)
{
    const knob_config_t cfg = {
        .default_direction = 1, .gpio_encoder_a = 4, .gpio_encoder_b = 5,
        .enable_power_save = true,
    };
    s_create_fail = argc > 1 && strcmp(argv[1], "--timer-failure") == 0;
    s_sample_during_registration = argc > 1 && strcmp(argv[1], "--early-sample") == 0;
    s_inject_interrupts = !s_create_fail;
    knob_handle_t knob = iot_knob_create(&cfg);
    if (s_create_fail) {
        assert(knob == NULL);
        assert(s_head_handle == NULL && s_knob_timer_handle == NULL);
        assert(s_allocations == 0 && s_registrations == 0 && s_gpio_resets == 2);
        assert(!s_is_timer_running && !s_timer.running && s_bad_starts == 0);
        puts("knob_power_save: timer failure cleans up before publication");
        return 0;
    }
    assert(knob);
    assert(s_bad_starts == 0); /* No ISR may start a nonexistent timer. */
    assert(s_timer.running == s_is_timer_running && s_starts > 0);
    assert(s_head_handle == knob && s_registrations == 2);
    if (!s_sample_during_registration)
        assert(!s_wake[4] && !s_wake[5]); /* Initialization must not undo the ISR. */
    assert(iot_knob_register_cb(knob, KNOB_LEFT, on_left, NULL) == ESP_OK);
    assert(iot_knob_register_cb(knob, KNOB_RIGHT, on_right, NULL) == ESP_OK);
    sample(8);
    assert(iot_knob_get_count_value(knob) == 0 && s_left == 0 && s_right == 0);
    assert(!s_timer.running && !s_is_timer_running); /* Idle power saving works. */
    level(4, 0);
    level(5, 0);
    assert(iot_knob_get_count_value(knob) == -1 && s_left == 1 && s_right == 0);
    level(5, 1);
    level(4, 1);
    assert(iot_knob_get_count_value(knob) == 0 && s_left == 1 && s_right == 1);
    puts("knob_power_save: immediate IRQs, idle wake, and both directions OK");
    return 0;
}
