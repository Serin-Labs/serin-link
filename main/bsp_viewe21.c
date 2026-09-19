/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 *
 * bsp_viewe21.c — VIEWE UEDX48480021-MD80ESP32 2.1" Touch-Knob Display (ESP32-S3R8).
 *   - ST7701S 480x480 RGB (bit-banged 3-wire SPI init + 16-bit parallel) on a PSRAM FB
 *   - CST816S capacitive touch (I2C, SCL15/SDA16)
 *   - rotary encoder PHA=6 / PHB=5 via PCNT; knob press = BOOT button on GPIO0
 *   - backlight enable on GPIO7 (active-low); RST direct on GPIO8 (no I/O expander)
 *
 * Pin map + ST7701S init table are authoritative from VIEWE's ESP-IDF reference
 * (examples/ESP-IDF/DX48480021ET-WB-A-LVGL/components/bsp/bsp.c, the touch variant).
 * There is no I/O expander: power, reset, backlight and
 * the knob button are wired straight to GPIOs, so this BSP hand-rolls the ST7701S
 * 3-wire init (vendor's proven byte sequence) instead of the esp_lcd_st7701
 * component. Color, orientation, pclk and backlight polarity are hardware-tunable.
 */
#include "bsp.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_cst816s.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "bsp_touch.h"
#include "bsp_common.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_timer.h"

#if !CONFIG_SPIRAM
#error "VIEWE 2.1 requires PSRAM. Build with -DSDKCONFIG_DEFAULTS=\"sdkconfig.defaults;sdkconfig.defaults.viewe21\""
#endif

static const char *TAG = "bsp_viewe21";

#define PIN_LCD_RST    8
#define PIN_LCD_BL     7
#define PIN_LCD_CS    18
#define PIN_LCD_SCK   13
#define PIN_LCD_SDA   12
#define PIN_LCD_DE    17
#define PIN_LCD_VSYNC  3
#define PIN_LCD_HSYNC 46
#define PIN_LCD_PCLK   9
#define RGB_DATA_PINS { 10, 11, 12, 13, 14,  21, 47, 48, 45, 38, 39,  40, 41, 42, 2, 1 }

#define I2C_PORT       I2C_NUM_0
#define PIN_I2C_SCL   15
#define PIN_I2C_SDA   16

#define PIN_ENC_A      5
#define PIN_ENC_B      6
#define ENC_PER_DETENT 2

#include "serin_input.h"
#define PIN_BTN        0
#define BTN_HOLD_MS  SERIN_HOLD_MS

#define LCD_W 480
#define LCD_H 480

#define BL_ON_LVL 0
#define BL_MODE  LEDC_LOW_SPEED_MODE
#define BL_TIMER LEDC_TIMER_0
#define BL_CH    LEDC_CHANNEL_0

static esp_lcd_panel_handle_t s_panel;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_touch_handle_t s_touch;

#define udelay(t)  esp_rom_delay_us(t)
#define CS(n)      gpio_set_level(PIN_LCD_CS,  n)
#define SCK(n)     gpio_set_level(PIN_LCD_SCK, n)
#define SDO(n)     gpio_set_level(PIN_LCD_SDA, n)

static void spi_write9(uint16_t data) {
    for (uint8_t n = 0; n < 9; n++) {
        SDO(data & 0x0100 ? 1 : 0);
        data <<= 1;
        SCK(0); udelay(10);
        SCK(1); udelay(10);
    }
}
static void spi_cmd(uint8_t cmd) {
    CS(0); udelay(10);
    spi_write9(cmd & 0x00FF);
    udelay(10); CS(1); SCK(1); SDO(1); udelay(10);
}
static void spi_dat(uint8_t dat) {
    CS(0); udelay(10);
    spi_write9(0x0100 | dat);
    udelay(10); CS(1); SCK(1); SDO(1); udelay(10);
}

typedef struct { uint8_t cmd; uint8_t data[52]; uint8_t n; } st7701_cmd_t;

/* VIEWE 2.1" (touch variant) ST7701S sequence, verbatim from vendor bsp.c.
 *
 * THIRD-PARTY TABLE: from VIEWESMART/UEDX48480021-MD80ESP32-2.1inch-Touch-Knob-Display,
 * MIT licensed, Copyright (c) 2025 VIEWE-CN. The MIT notice it is redistributed
 * under is in `LICENSE-VIEWE.txt` beside this file. MIT is GPL-3.0-compatible,
 * so the file as a whole stays GPL-3.0-only.
 *
 * n == 0xFF terminates. 0x3A=0x66 (RGB666 input) and 0xC7=0x00 are VIEWE's
 * empirical fixes — do not "correct" them. */
static const st7701_cmd_t ST7701_INIT[] = {
    {0xFF, {0x77,0x01,0x00,0x00,0x13}, 5},
    {0xEF, {0x08}, 1},
    {0xFF, {0x77,0x01,0x00,0x00,0x10}, 5},
    {0xC0, {0x3B,0x00}, 2},
    {0xC1, {0x0B,0x02}, 2},
    {0xC2, {0x07,0x02}, 2},
    {0xC7, {0x00}, 1},
    {0xCC, {0x10}, 1},
    {0xCD, {0x08}, 1},
    {0xB0, {0x00,0x11,0x16,0x0E,0x11,0x06,0x05,0x09,0x08,0x21,0x06,0x13,0x10,0x29,0x31,0x18}, 16},
    {0xB1, {0x00,0x11,0x16,0x0E,0x11,0x07,0x05,0x09,0x09,0x21,0x05,0x13,0x11,0x2A,0x31,0x18}, 16},
    {0xFF, {0x77,0x01,0x00,0x00,0x11}, 5},
    {0xB0, {0x6D}, 1},
    {0xB1, {0x37}, 1},
    {0xB2, {0x8B}, 1},
    {0xB3, {0x80}, 1},
    {0xB5, {0x43}, 1},
    {0xB7, {0x85}, 1},
    {0xB8, {0x20}, 1},
    {0xC0, {0x09}, 1},
    {0xC1, {0x78}, 1},
    {0xC2, {0x78}, 1},
    {0xD0, {0x88}, 1},
    {0xE0, {0x00,0x00,0x02}, 3},
    {0xE1, {0x03,0xA0,0x00,0x00,0x04,0xA0,0x00,0x00,0x00,0x20,0x20}, 11},
    {0xE2, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 13},
    {0xE3, {0x00,0x00,0x11,0x00}, 4},
    {0xE4, {0x22,0x00}, 2},
    {0xE5, {0x05,0xEC,0xF6,0xCA,0x07,0xEE,0xF6,0xCA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 16},
    {0xE6, {0x00,0x00,0x11,0x00}, 4},
    {0xE7, {0x22,0x00}, 2},
    {0xE8, {0x06,0xED,0xF6,0xCA,0x08,0xEF,0xF6,0xCA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, 16},
    {0xE9, {0x36,0x00}, 2},
    {0xEB, {0x00,0x00,0x40,0x40,0x00,0x00,0x00}, 7},
    {0xED, {0xFF,0xFF,0xFF,0xBA,0x0A,0xFF,0x45,0xFF,0xFF,0x54,0xFF,0xA0,0xAB,0xFF,0xFF,0xFF}, 16},
    {0xEF, {0x08,0x08,0x08,0x45,0x3F,0x54}, 6},
    {0xFF, {0x77,0x01,0x00,0x00,0x13}, 5},
    {0xE8, {0x00,0x0E}, 2},
    {0xFF, {0x77,0x01,0x00,0x00,0x00}, 5},
    {0x11, {0x00}, 1},
    {0xFF, {0x77,0x01,0x00,0x00,0x13}, 5},
    {0xE8, {0x00,0x0C}, 2},
    {0xE8, {0x00,0x00}, 2},
    {0xFF, {0x77,0x01,0x00,0x00,0x00}, 5},
    {0x36, {0x00}, 1},
    {0x3A, {0x66}, 1},
    {0x00, {0x00}, 0xFF},
};

static void lcd_init_st7701(void) {
    gpio_config_t io = {
        .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pin_bit_mask = BIT64(PIN_LCD_CS) | BIT64(PIN_LCD_SCK) |
                        BIT64(PIN_LCD_SDA) | BIT64(PIN_LCD_RST),
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_LCD_RST, 1);
    CS(1); SCK(1); SDO(1);

    for (int i = 0; ST7701_INIT[i].n != 0xFF; i++) {
        spi_cmd(ST7701_INIT[i].cmd);
        for (int j = 0; j < ST7701_INIT[i].n; j++) spi_dat(ST7701_INIT[i].data[j]);
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    spi_cmd(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    gpio_reset_pin(PIN_LCD_SCK);
    gpio_reset_pin(PIN_LCD_SDA);
}

static void backlight_init(void) {
    ledc_timer_config_t t = { .speed_mode = BL_MODE, .timer_num = BL_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = 30000, .clk_cfg = LEDC_AUTO_CLK };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    ledc_channel_config_t c = { .gpio_num = PIN_LCD_BL, .speed_mode = BL_MODE,
        .channel = BL_CH, .timer_sel = BL_TIMER,
        .duty = (BL_ON_LVL == 0) ? 255 : 0, .hpoint = 0 };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}
static int s_bl_pct = 100;

static void backlight_apply(int pct) {
    uint32_t duty = (BL_ON_LVL == 0) ? (255 * (100 - pct)) / 100 : (255 * pct) / 100;
    ledc_set_duty(BL_MODE, BL_CH, duty);
    ledc_update_duty(BL_MODE, BL_CH);
}

void bsp_backlight_set(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_bl_pct = pct;
    backlight_apply(pct);
}

void bsp_display_sleep(bool sleep) {
    backlight_apply(sleep ? 0 : s_bl_pct);
}

lv_display_t *bsp_display_start(void) {
    ESP_LOGI(TAG, "ST7701S 3-wire init");
    lcd_init_st7701();

    ESP_LOGI(TAG, "RGB panel");
    esp_lcd_rgb_panel_config_t rgb_cfg = {
        .clk_src = LCD_CLK_SRC_PLL160M,
        .psram_trans_align = 64,
        .data_width = 16, .bits_per_pixel = 16,
        .de_gpio_num = PIN_LCD_DE, .pclk_gpio_num = PIN_LCD_PCLK,
        .vsync_gpio_num = PIN_LCD_VSYNC, .hsync_gpio_num = PIN_LCD_HSYNC,
        .disp_gpio_num = -1,
        .data_gpio_nums = RGB_DATA_PINS,
        .timings = {
            .pclk_hz = 18 * 1000 * 1000,
            .h_res = LCD_W, .v_res = LCD_H,
            .hsync_pulse_width = 8, .hsync_back_porch = 20, .hsync_front_porch = 40,
            .vsync_pulse_width = 8, .vsync_back_porch = 20, .vsync_front_porch = 50,
            .flags.pclk_active_neg = false,
        },
        .num_fbs = 2,
        .bounce_buffer_size_px = LCD_W * 30,
        .dma_burst_size = 64,
        .flags = { .fb_in_psram = 1 },
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&rgb_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    backlight_init();

    ESP_LOGI(TAG, "lvgl_port");
    const lvgl_port_cfg_t lp = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lp));
    const lvgl_port_display_cfg_t dcfg = {
        .panel_handle = s_panel,
        .buffer_size = LCD_W * LCD_H, .double_buffer = true,
        .hres = LCD_W, .vres = LCD_H, .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = { .buff_dma = true, .buff_spiram = false, .swap_bytes = false,
                   .direct_mode = true },
    };
    const lvgl_port_display_rgb_cfg_t rgb_lvgl = {
        .flags = { .bb_mode = false, .avoid_tearing = true },
    };
    /* See bsp_viewe.c: taskLVGL may render as soon as add_disp drops the lock,
     * so every LVGL call on the bring-up path holds the (recursive) mutex. */
    ESP_ERROR_CHECK(lvgl_port_lock(0) ? ESP_OK : ESP_ERR_TIMEOUT);
    lv_display_t *disp = lvgl_port_add_disp_rgb(&dcfg, &rgb_lvgl);
    ESP_LOGI(TAG, "LVGL disp %dx%d", (int)lv_display_get_horizontal_resolution(disp),
             (int)lv_display_get_vertical_resolution(disp));
    lvgl_port_unlock();

    ESP_LOGI(TAG, "touch CST816S");
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT, .sda_io_num = PIN_I2C_SDA, .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io));
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_W, .y_max = LCD_H,
        .rst_gpio_num = -1, .int_gpio_num = -1,
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 0 },
    };
    if (esp_lcd_touch_new_i2c_cst816s(tp_io, &tp_cfg, &s_touch) == ESP_OK) {
        const lvgl_port_touch_cfg_t tcfg = { .disp = disp, .handle = s_touch };
        lv_indev_t *ti = lvgl_port_add_touch(&tcfg);
        bsp_touch_attach(ti, s_touch, NULL);
    } else {
        s_touch = NULL;
        ESP_LOGW(TAG, "CST816S init failed — continuing without touch");
    }

    ESP_LOGI(TAG, "display ready");
    return disp;
}

static void (*s_on_enc)(int);
static void (*s_on_btn)(void);
static void (*s_on_btn_hold)(void);
static void (*s_on_down)(void);
static void (*s_on_up)(void);
static pcnt_unit_handle_t s_pcnt;

bool bsp_button_pressed(void) { return bsp_btn_is_pressed(PIN_BTN); }

static void input_task(void *arg) {
    int last = 0;
    pcnt_unit_get_count(s_pcnt, &last);
    int btn_stable = 1, btn_last = 1; TickType_t t_change = 0, t_press = 0; int hold_fired = 0;
    for (;;) {
        int cnt = 0;
        pcnt_unit_get_count(s_pcnt, &cnt);
        int diff = (cnt - last) / ENC_PER_DETENT;
        if (diff != 0) {
            last += diff * ENC_PER_DETENT;
            bsp_ui_call_i(s_on_enc, diff);
        }
        int raw = gpio_get_level(PIN_BTN) ? 1 : 0;
        if (raw != btn_last) { btn_last = raw; t_change = xTaskGetTickCount(); }
        if (raw != btn_stable && (xTaskGetTickCount() - t_change) > pdMS_TO_TICKS(25)) {
            btn_stable = raw;
            if (btn_stable == 0) {
                t_press = xTaskGetTickCount(); hold_fired = 0;
                bsp_ui_call(s_on_down);
            } else {
                bsp_ui_call(s_on_up);
                if (!hold_fired) bsp_ui_call(s_on_btn);
            }
        }
        if (btn_stable == 0 && !hold_fired &&
            (xTaskGetTickCount() - t_press) > pdMS_TO_TICKS(BTN_HOLD_MS)) {
            hold_fired = 1;
            bsp_ui_call(s_on_btn_hold);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void bsp_input_start(void (*on_encoder)(int), void (*on_button)(void),
                     void (*on_button_hold)(void),
                     void (*on_press_down)(void), void (*on_press_up)(void)) {
    s_on_enc = on_encoder;
    s_on_btn = on_button;
    s_on_btn_hold = on_button_hold;
    s_on_down = on_press_down;
    s_on_up = on_press_up;

    bsp_btn_gpio_ensure(PIN_BTN);

    pcnt_unit_config_t ucfg = { .high_limit = 1000, .low_limit = -1000 };
    ESP_ERROR_CHECK(pcnt_new_unit(&ucfg, &s_pcnt));
    pcnt_glitch_filter_config_t gf = { .max_glitch_ns = 1000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &gf));

    pcnt_chan_config_t ch_a = { .edge_gpio_num = PIN_ENC_A, .level_gpio_num = PIN_ENC_B };
    pcnt_channel_handle_t cha = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &ch_a, &cha));
    pcnt_channel_set_edge_action(cha, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(cha, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_chan_config_t ch_b = { .edge_gpio_num = PIN_ENC_B, .level_gpio_num = PIN_ENC_A };
    pcnt_channel_handle_t chb = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &ch_b, &chb));
    pcnt_channel_set_edge_action(chb, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chb, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt));

    xTaskCreate(input_task, "input", 4096, NULL, 5, NULL);
}

void bsp_register_console(void) {}
