/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "bsp.h"
#include "ui.h"
#include "espnow_client.h"
#include "link_ota.h"
#include "perf_stats.h"
#include "factory_reset.h"
#include "sensor_sht40.h"
#include "thermal_cal.h"
#include "wdt_crumb.h"
#include "esp_lvgl_port.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "dial_main";

static void on_encoder(int delta) { ui_encoder(delta); }
static void on_button(void)       { ui_button(); }
static void on_long(void) { ui_button_long(); }

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    wdt_crumb_boot();

    lv_display_t *disp = bsp_display_start();
    perf_stats_init(disp);
    bsp_backlight_set(100);

    factory_reset_boot_check();

    if (lvgl_port_lock(0)) {
        /* Before ui_init: the splash fades with the light when it can. */
#if CONFIG_SERIN_BOARD_VIEWE_15
        ui_set_on_bright_scale(bsp_backlight_scale);
#endif
        ui_init();
        ui_set_on_edit(espnow_client_on_local_edit);
        ui_set_on_units(espnow_client_on_units_edit);
        ui_set_on_brightness(bsp_backlight_set);
        ui_set_on_display_sleep(bsp_display_sleep);
        ui_set_on_pair(espnow_client_begin_pairing);
        ui_set_on_pair_cancel(espnow_client_cancel_pairing);
        ui_set_on_forget(espnow_client_forget);
        ui_set_on_zone_select(espnow_client_set_active);
        ui_set_on_zones_open(espnow_client_refresh_idents);
        ui_set_on_wifi_setup(espnow_client_wifi_setup);
        ui_set_on_wifi_setup_session(espnow_client_wifi_setup_session);
        ui_set_on_wifi_cancel(espnow_client_wifi_cancel);
        ui_set_on_room_source_id(espnow_client_on_room_source_id);
        ui_set_fw_version(esp_app_get_description()->version);
        ui_set_fw_build(esp_app_get_description()->date);
        ui_set_on_ota_check(link_ota_begin);
        ui_set_on_ota_confirm(link_ota_confirm);
        ui_set_on_ota_cancel(link_ota_cancel);
        hvac_state_t s; ui_get_state(&s);
        s.alert = ALERT_NOLINK;
        ui_set_state(&s);
        espnow_client_boot_ui();
        lvgl_port_unlock();
    }

    bsp_input_start(on_encoder, on_button, on_long, ui_button_down, ui_button_up);
    sensor_sht40_init();
    espnow_client_start();
    link_ota_init();
    thermal_cal_start();

    ESP_LOGI(TAG, "main stack headroom %u bytes",
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
