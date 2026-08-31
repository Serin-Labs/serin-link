/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "bsp_touch.h"
#include "touch_retry.h"
#include "serin_input.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "bsp_touch";

static esp_lcd_touch_handle_t s_tp;
static void (*s_recover)(void);
static touch_retry_t s_rt;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->state = LV_INDEV_STATE_RELEASED;
    if (!s_tp) return;

    const int64_t now = esp_timer_get_time();
    if (!touch_retry_should_read(&s_rt, now)) return;
    touch_retry_note_read(&s_rt, now);

    uint8_t cnt = 0;
    esp_lcd_touch_point_data_t pts[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
    esp_err_t err = esp_lcd_touch_read_data(s_tp);
    if (err == ESP_OK)
        err = esp_lcd_touch_get_data(s_tp, pts, &cnt, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);

    if (err != ESP_OK) {
        if (touch_retry_note_fail(&s_rt) != TOUCH_ACT_RECOVER) return;
        if (s_rt.recoveries == 1)
            ESP_LOGW(TAG, "touch read failed (%s) x%d — %s", esp_err_to_name(err),
                     TOUCH_RETRY_FAIL_LIMIT,
                     s_recover ? "resetting" : "no reset line on this board");
        if (!s_recover) return;
        s_recover();
        if (s_rt.recoveries == TOUCH_RETRY_MAX_RECOVERIES)
            ESP_LOGW(TAG, "touch did not answer %d resets — probing only from here",
                     TOUCH_RETRY_MAX_RECOVERIES);
        touch_retry_retry_now(&s_rt);
        return;
    }

    if (touch_retry_note_ok(&s_rt)) ESP_LOGI(TAG, "touch recovered");
    if (cnt > 0) {
        data->point.x = pts[0].x;
        data->point.y = pts[0].y;
        data->state   = LV_INDEV_STATE_PRESSED;
    }
}

void bsp_touch_attach(lv_indev_t *indev, esp_lcd_touch_handle_t tp,
                      void (*recover)(void))
{
    if (!indev || !tp) return;
    s_tp      = tp;
    s_recover = recover;
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "could not take the LVGL lock — touch stays on the port's "
                      "read callback, which reboots on an I2C error");
        return;
    }
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_long_press_time(indev, SERIN_HOLD_MS);
    lvgl_port_unlock();
}
