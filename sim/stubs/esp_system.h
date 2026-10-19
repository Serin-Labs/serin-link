// SPDX-License-Identifier: GPL-3.0-only
#pragma once
typedef enum { ESP_RST_UNKNOWN = 0, ESP_RST_POWERON = 1 } esp_reset_reason_t;
esp_reset_reason_t esp_reset_reason(void);
