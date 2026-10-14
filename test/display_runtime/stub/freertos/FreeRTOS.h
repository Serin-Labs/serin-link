#pragma once
#include <stdint.h>
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
typedef int BaseType_t;
typedef uint32_t TickType_t;
