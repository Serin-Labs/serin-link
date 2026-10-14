#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define LVGL_PORT_EVENT_USER 0x80
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);
esp_err_t lvgl_port_stop(void);
esp_err_t lvgl_port_task_wake(int event, void *param);
