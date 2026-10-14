#pragma once
#include <stdbool.h>
#include "esp_err.h"
typedef void *esp_lcd_panel_io_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
typedef struct {
    bool (*on_color_trans_done)(esp_lcd_panel_io_handle_t,
                               esp_lcd_panel_io_event_data_t *, void *);
} esp_lcd_panel_io_callbacks_t;
esp_err_t esp_lcd_panel_io_register_event_callbacks(
    esp_lcd_panel_io_handle_t, const esp_lcd_panel_io_callbacks_t *, void *);
