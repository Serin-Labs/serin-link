/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 *
 * bsp_viewe.c — VIEWE UEDX46460015-MD50ET 1.5" Touch-Knob Display (ESP32-S3R8).
 *   - SH8601 466x466 visible (panel RAM 472x466) QSPI AMOLED on SPI2_HOST
 *   - CST820 capacitive touch (legacy I2C0: SDA=1 SCL=3 RST=2 INT=4)
 *   - rotary encoder A=6/B=5 via espressif `knob`; BOOT button GPIO0 via `button`
 *   - brightness: panel command 0x51 (AMOLED dimming); GPIO17 = VCI-EN panel power
 *
 * Pin map + init sequence are authoritative from VIEWE's ESP-IDF reference
 * examples/ESP-IDF/lvgl_port_v9_knob_15/main/main.c. The marketing README's pin
 * table is wrong (it puts touch SDA on GPIO0); the reference source rules.
 */
#include "bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst820.h"
#include "esp_lvgl_port.h"
#include "display_runtime.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "prefs.h"
#include <string.h>
#include <stdlib.h>
#include "bsp_touch.h"
#include "bsp_common.h"
#include "ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "iot_knob.h"
#include "iot_button.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if !CONFIG_SPIRAM
#error "VIEWE board requires PSRAM. Build with -DSDKCONFIG_DEFAULTS=\"sdkconfig.defaults;sdkconfig.defaults.viewe\""
#endif

static const char *TAG = "bsp_viewe";

#define LCD_HOST       SPI2_HOST
#define PIN_LCD_CS     12
#define PIN_LCD_PCLK   10
#define PIN_LCD_DATA0  13
#define PIN_LCD_DATA1  11
#define PIN_LCD_DATA2  14
#define PIN_LCD_DATA3   9
#define PIN_LCD_RST     8
#define PIN_VCI_EN     17

#define LCD_W         466
#define LCD_H         466
#define LCD_BPP        16
#define LCD_BUF_ROWS   CONFIG_SERIN_LCD_BUFFER_ROWS
#define LCD_STRIPE_SZ (LCD_W * LCD_BUF_ROWS * LCD_BPP / 8)
_Static_assert(LCD_BUF_ROWS % 2 == 0, "QSPI draw-buffer rows must be even");

#define TP_I2C_PORT   I2C_NUM_0
#define PIN_TP_SDA      1
#define PIN_TP_SCL      3
#define PIN_TP_RST      2
#define PIN_TP_INT      4
#define TP_BOOT_MS    100

#define PIN_ENC_A       6
#define PIN_ENC_B       5
#define PIN_BTN         0
#include "serin_input.h"
#define BTN_HOLD_MS   SERIN_HOLD_MS

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_touch_handle_t    s_touch;

/* SH8601 sequence from VIEWE's 1.5" ESP-IDF reference.
 *
 * THIRD-PARTY TABLE: from VIEWESMART/UEDX46460015-MD50ESP32-1.5inch-Touch-Knob-Display,
 * MIT licensed, Copyright (c) 2025 VIEWE-CN. The MIT notice it is redistributed
 * under is in `LICENSE-VIEWE.txt` beside this file. MIT is GPL-3.0-compatible,
 * so the file as a whole stays GPL-3.0-only. */
static const sh8601_lcd_init_cmd_t s_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 0, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 0, 10},
    {0x53, (uint8_t[]){0x20}, 1, 10},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x63, (uint8_t[]){0xFF}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xDD}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 60},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static esp_err_t viewe_tx_cmd(uint8_t cmd, const uint8_t *param, size_t n) {
    int word = (0x02 << 24) | ((int)cmd << 8);
    return esp_lcd_panel_io_tx_param(s_io, word, param, n);
}

static int s_bl_pct = 100;
static bool s_first_frame;
static bool s_in_slpin;
static bool s_dark;

/* Transition dip: 255 = the user's level, 0 = black.  The level is raised to
 * s_dip_curve so a linear ramp reads as an even fade.  0x53 bit 3 is the DCS
 * "display dimming" flag; it is left off because the panel's own smoothing
 * lags the ramp, so the new screen could appear before the light reached 0.
 * Both stay runtime-only bench levers (`bl-dip`). */
static uint8_t s_scale = 255;
static uint8_t s_dip_curve = 2;
static uint8_t s_bl_ctrl = 0x24;

static void backlight_apply(int pct) {
    if (s_io && s_first_frame && !s_in_slpin) {
        viewe_tx_cmd(0x53, &s_bl_ctrl, 1);
        uint32_t f = 255;
        for (int i = 0; i < s_dip_curve; i++) f = f * s_scale / 255;
        uint8_t b = (uint8_t)((255 * pct / 100) * f / 255);
        viewe_tx_cmd(0x51, &b, 1);
    }
}

void bsp_backlight_scale(uint8_t scale) {
    if (scale == s_scale) return;
    s_scale = scale;
    if (!s_dark) backlight_apply(s_bl_pct);
}

void bsp_backlight_set(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_bl_pct = pct;
    backlight_apply(pct);
}

void bsp_display_deep_off(bool off) {
    if (!s_io) return;
    if (off) {
        viewe_tx_cmd(0x28, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        viewe_tx_cmd(0x10, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        viewe_tx_cmd(0x11, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        viewe_tx_cmd(0x29, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        backlight_apply(s_bl_pct);
    }
}

#if !CONFIG_SERIN_THERMAL_CAL
#define PANEL_DEEP_PREF "panel_deep"
static bool  s_deep_pref;
static bool  s_deep_pref_loaded;

enum { PW_SLEEP, PW_WAKE };
static QueueHandle_t s_pw_q;
#define PANEL_TASK_STACK 3072

static void pw_cmd(uint8_t cmd) {
    if (lvgl_port_lock(0)) { viewe_tx_cmd(cmd, NULL, 0); lvgl_port_unlock(); }
}

static void panel_task(void *arg) {
    (void)arg;
    uint8_t op;
    bool asleep = false;
    uint32_t low = UINT32_MAX;
    for (;;) {
        if (xQueueReceive(s_pw_q, &op, portMAX_DELAY) != pdTRUE) continue;
        if (op == PW_SLEEP && !asleep) {
            if (lvgl_port_lock(0)) { s_in_slpin = true; lvgl_port_unlock(); }
            pw_cmd(0x28);
            vTaskDelay(pdMS_TO_TICKS(20));
            pw_cmd(0x10);
            vTaskDelay(pdMS_TO_TICKS(120));
            asleep = true;
        } else if (op == PW_WAKE && asleep) {
            pw_cmd(0x11);
            vTaskDelay(pdMS_TO_TICKS(120));
            pw_cmd(0x29);
            vTaskDelay(pdMS_TO_TICKS(20));
            if (lvgl_port_lock(0)) {
                s_in_slpin = false;
                backlight_apply(s_bl_pct);
                lvgl_port_unlock();
            }
            asleep = false;
        }
        uint32_t hw = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
        if (hw < low) {
            low = hw;
            ESP_LOGI(TAG, "panel task stack low-water %lu free of %d",
                     (unsigned long)hw, PANEL_TASK_STACK);
        }
    }
}

static bool panel_deep(void) {
    if (!s_deep_pref_loaded) {
        s_deep_pref = prefs_get_bool(PANEL_DEEP_PREF, true);
        s_deep_pref_loaded = true;
    }
    if (!s_deep_pref) return false;
    if (!s_pw_q) {
        s_pw_q = xQueueCreate(2, sizeof(uint8_t));
        if (!s_pw_q || xTaskCreate(panel_task, "panel", PANEL_TASK_STACK,
                                   NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "panel task failed (largest internal block %u) — deep sleep off this boot",
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            if (s_pw_q) { vQueueDelete(s_pw_q); s_pw_q = NULL; }
            s_deep_pref = false;
            return false;
        }
    }
    return true;
}

static void panel_post(uint8_t op) {
    if (xQueueSend(s_pw_q, &op, 0) != pdTRUE)
        ESP_LOGW(TAG, "panel queue full — %s dropped", op == PW_SLEEP ? "sleep" : "wake");
}

static int cmd_panel_sleep(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "deep") == 0)         prefs_set_bool(PANEL_DEEP_PREF, true);
        else if (strcmp(argv[1], "shallow") == 0) prefs_set_bool(PANEL_DEEP_PREF, false);
        else { printf("usage: panel-sleep [shallow|deep]\n"); return 1; }
        s_deep_pref_loaded = false;
    }
    printf("panel-sleep: %s  (panel now: %s)\n",
           prefs_get_bool(PANEL_DEEP_PREF, true) ? "deep (SLPIN)" : "shallow (brightness 0)",
           s_in_slpin ? "in SLPIN" : "powered");
    return 0;
}

static int cmd_bl_dip(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "curve") == 0) {
            int c = atoi(argv[i + 1]);
            if (c < 1 || c > 4) { printf("curve is 1-4\n"); return 1; }
            s_dip_curve = (uint8_t)c;
        } else if (strcmp(argv[i], "out") == 0 || strcmp(argv[i], "in") == 0) {
            uint32_t o, n;
            ui_dip_ms_get(&o, &n);
            uint32_t v = (uint32_t)atoi(argv[i + 1]);
            if (argv[i][0] == 'o') o = v; else n = v;
            ui_dip_ms_set(o, n);
        } else if (strcmp(argv[i], "dd") == 0) {
            bool on = strcmp(argv[i + 1], "on") == 0;
            s_bl_ctrl = on ? (s_bl_ctrl | 0x08) : (s_bl_ctrl & ~0x08);
            if (lvgl_port_lock(0)) { backlight_apply(s_bl_pct); lvgl_port_unlock(); }
        } else {
            printf("usage: bl-dip [out ms] [in ms] [curve 1-4] [dd on|off]\n");
            return 1;
        }
    }
    uint32_t o, n;
    ui_dip_ms_get(&o, &n);
    printf("bl-dip: out %lu ms  in %lu ms  curve %u  dd %s  (0x53=0x%02X)\n",
           (unsigned long)o, (unsigned long)n, s_dip_curve,
           (s_bl_ctrl & 0x08) ? "on" : "off", s_bl_ctrl);
    return 0;
}

void bsp_register_console(void) {
    const esp_console_cmd_t c = {
        .command = "panel-sleep",
        .help    = "Idle screen = Off depth (bench lever): panel-sleep [shallow|deep]",
        .func    = cmd_panel_sleep,
    };
    esp_console_cmd_register(&c);
    const esp_console_cmd_t d = {
        .command = "bl-dip",
        .help    = "Transition brightness dip (bench, not saved): bl-dip [out ms] [in ms] [curve 1-4] [dd on|off]",
        .func    = cmd_bl_dip,
    };
    esp_console_cmd_register(&d);
}
#else
void bsp_register_console(void) {}
#endif

void bsp_display_sleep(bool sleep) {
    s_dark = sleep;
    backlight_apply(sleep ? 0 : s_bl_pct);
#if !CONFIG_SERIN_THERMAL_CAL
    if (sleep ? panel_deep() : (s_pw_q != NULL))
        panel_post(sleep ? PW_SLEEP : PW_WAKE);
#endif
}

static void tp_reset_pulse(void) {
    gpio_set_level(PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TP_RST, 1);
}
static void tp_recover(void) {
    tp_reset_pulse();
    vTaskDelay(pdMS_TO_TICKS(60));
}

static void first_frame_cb(lv_event_t *e) {
    lv_display_t *d = lv_event_get_target(e);
    s_first_frame = true;
    backlight_apply(s_bl_pct);
    lv_display_remove_event_cb_with_user_data(d, first_frame_cb, NULL);
}

static void rounder_cb(lv_event_t *e) {
    lv_area_t *a = lv_event_get_invalidated_area(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

lv_display_t *bsp_display_start(void) {
    gpio_config_t bk = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_VCI_EN };
    ESP_ERROR_CHECK(gpio_config(&bk));
    gpio_set_level(PIN_VCI_EN, 1);

    gpio_config_t tprst = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_TP_RST };
    ESP_ERROR_CHECK(gpio_config(&tprst));
    tp_reset_pulse();
    const TickType_t tp_released = xTaskGetTickCount();

    ESP_LOGI(TAG, "QSPI bus");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_PCLK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
        LCD_STRIPE_SZ);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "panel IO (QSPI)");
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 80 * 1000 * 1000,
        .trans_queue_depth = 20,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = { .quad_mode = true },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io));

    ESP_LOGI(TAG, "SH8601 panel");
    sh8601_vendor_config_t vendor = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BPP,
        .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 6, 0));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    ESP_LOGI(TAG, "touch CST820");
    {
        TickType_t el = xTaskGetTickCount() - tp_released;
        if (el < pdMS_TO_TICKS(TP_BOOT_MS)) vTaskDelay(pdMS_TO_TICKS(TP_BOOT_MS) - el);
    }
    const i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = PIN_TP_SDA, .scl_io_num = PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_cfg, &tp_io));
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_W, .y_max = LCD_H,
        .rst_gpio_num = -1  , .int_gpio_num = PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    if (esp_lcd_touch_new_i2c_cst820(tp_io, &tp_cfg, &s_touch) != ESP_OK) {
        ESP_LOGW(TAG, "CST820 init failed — continuing without touch");
        s_touch = NULL;
    }

    ESP_LOGI(TAG, "lvgl_port");
    const lvgl_port_cfg_t lp = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lp));
    ESP_ERROR_CHECK(display_runtime_start());
    const lvgl_port_display_cfg_t dcfg = {
        .io_handle = s_io, .panel_handle = s_panel,
        .buffer_size = LCD_W * LCD_BUF_ROWS, .double_buffer = true,
        .hres = LCD_W, .vres = LCD_H, .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565_SWAPPED,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .buff_dma = true, .swap_bytes = false },
    };
    /* Hold the LVGL mutex across bring-up. lv_display_create() arms an immediate
     * first refresh, and taskLVGL runs at priority 4 unpinned, so it can start
     * rendering the moment lvgl_port_add_disp() drops the lock internally —
     * touching the screen after that trips LVGL's invalidate-during-render
     * assert, which is a silent while(1) here (LV_USE_LOG is off). The port
     * mutex is recursive, so the nested take inside add_disp is fine. */
    ESP_ERROR_CHECK(lvgl_port_lock(0) ? ESP_OK : ESP_ERR_TIMEOUT);
    lv_display_t *disp = lvgl_port_add_disp(&dcfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_ERR_NO_MEM);
#if CONFIG_SERIN_LCD_BLOCKING_FLUSH
    ESP_ERROR_CHECK(display_runtime_attach(disp, s_io, true));
#else
    ESP_ERROR_CHECK(display_runtime_attach(disp, s_io, false));
#endif
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_obj_t *scr0 = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr0, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr0, LV_OPA_COVER, 0);
    lv_display_add_event_cb(disp, first_frame_cb, LV_EVENT_RENDER_READY, NULL);
    lvgl_port_unlock();

    if (s_touch) {
        const lvgl_port_touch_cfg_t tcfg = { .disp = disp, .handle = s_touch };
        lv_indev_t *ti = lvgl_port_add_touch(&tcfg);
        bsp_touch_attach(ti, s_touch, tp_recover);
    }

    ESP_LOGI(TAG, "display ready %dx%d", LCD_W, LCD_H);
    return disp;
}

static void (*s_on_enc)(int);
static void (*s_on_btn)(void);
static void (*s_on_hold)(void);
static void (*s_on_down)(void);
static void (*s_on_up)(void);

bool bsp_button_pressed(void) { return bsp_btn_is_pressed(PIN_BTN); }

static knob_handle_t s_knob;

typedef enum {
    EV_ROTATE = 0,
    EV_CLICK,
    EV_HOLD,
    EV_DOWN,
    EV_UP,
} ev_kind_t;

typedef struct { uint8_t kind; int8_t arg; } in_ev_t;

#define INPUT_Q_DEPTH      32
#define INPUT_BTN_RESERVE   8

static QueueHandle_t s_ev_q;
static volatile bool s_up_latched;

static void ev_apply(const in_ev_t *ev) {
    switch (ev->kind) {
    case EV_ROTATE: bsp_ui_call_i(s_on_enc, ev->arg); break;
    case EV_CLICK:  bsp_ui_call(s_on_btn);            break;
    case EV_HOLD:   bsp_ui_call(s_on_hold);           break;
    case EV_DOWN:   bsp_ui_call(s_on_down);           break;
    case EV_UP:     bsp_ui_call(s_on_up);             break;
    default: break;
    }
}

static void ev_push(ev_kind_t kind, int arg) {
    if (!s_ev_q) return;
    if (kind == EV_ROTATE && uxQueueSpacesAvailable(s_ev_q) <= INPUT_BTN_RESERVE) return;
    in_ev_t ev = { .kind = (uint8_t)kind, .arg = (int8_t)arg };
    if (xQueueSend(s_ev_q, &ev, 0) != pdTRUE && kind == EV_UP) s_up_latched = true;
}

#define ENC_TASK_STACK 8192

static void enc_task(void *arg) {
    in_ev_t ev;
    for (;;) {
        if (xQueueReceive(s_ev_q, &ev, portMAX_DELAY) != pdTRUE) continue;
        ev_apply(&ev);
        static uint32_t low = UINT32_MAX;
        uint32_t hw = uxTaskGetStackHighWaterMark(NULL);
        if (hw < low) {
            low = hw;
            ESP_LOGI(TAG, "enc stack headroom %u of %u bytes", (unsigned)hw,
                     (unsigned)ENC_TASK_STACK);
        }
        if (s_up_latched && uxQueueMessagesWaiting(s_ev_q) == 0) {
            s_up_latched = false;
            const in_ev_t up = { .kind = EV_UP, .arg = 0 };
            ev_apply(&up);
        }
    }
}

static void knob_cb(void *arg, void *data) {
    ev_push(EV_ROTATE, (data == (void *)KNOB_RIGHT) ? +1 : -1);
}
static void btn_click_cb(void *arg, void *data) { ev_push(EV_CLICK, 0); }
static void btn_long_cb(void *arg, void *data)  { ev_push(EV_HOLD,  0); }
static void btn_down_cb(void *arg, void *data)  { ev_push(EV_DOWN,  0); }
static void btn_up_cb(void *arg, void *data)    { ev_push(EV_UP,    0); }

void bsp_input_start(void (*on_encoder)(int), void (*on_button)(void),
                     void (*on_button_hold)(void),
                     void (*on_press_down)(void), void (*on_press_up)(void)) {
    s_on_enc = on_encoder;
    s_on_btn = on_button;
    s_on_hold = on_button_hold;
    s_on_down = on_press_down;
    s_on_up = on_press_up;

    s_ev_q = xQueueCreate(INPUT_Q_DEPTH, sizeof(in_ev_t));
    if (s_ev_q) {
        xTaskCreate(enc_task, "enc", ENC_TASK_STACK, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "input queue alloc failed — knob and button are dead");
    }

    knob_config_t kcfg = {
        .default_direction = 1,
        .gpio_encoder_a = PIN_ENC_A,
        .gpio_encoder_b = PIN_ENC_B,
#if CONFIG_SERIN_INPUT_POWER_SAVE
        .enable_power_save = true,
#endif
    };
    s_knob = iot_knob_create(&kcfg);
    if (s_knob) {
        iot_knob_register_cb(s_knob, KNOB_RIGHT, knob_cb, (void *)KNOB_RIGHT);
        iot_knob_register_cb(s_knob, KNOB_LEFT, knob_cb, (void *)KNOB_LEFT);
    } else {
        ESP_LOGW(TAG, "knob init failed");
    }

    button_config_t bcfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = BTN_HOLD_MS,
        .gpio_button_config = {
            .gpio_num = PIN_BTN, .active_level = 0,
#if CONFIG_SERIN_INPUT_POWER_SAVE
            .enable_power_save = true,
#endif
        },
    };
    button_handle_t btn = iot_button_create(&bcfg);
    if (btn) {
        iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, btn_click_cb, NULL);
        iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, btn_long_cb, NULL);
        iot_button_register_cb(btn, BUTTON_PRESS_DOWN, btn_down_cb, NULL);
        iot_button_register_cb(btn, BUTTON_PRESS_UP, btn_up_cb, NULL);
    } else {
        ESP_LOGW(TAG, "button init failed");
    }
}
