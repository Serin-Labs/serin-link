/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include "wdt_crumb.h"
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "wdt_crumb";
static const char *NS  = "serin";
static const char *KEY = "wdt_last";

#define CRUMB_MAGIC 0x57445443u
#define CRUMB_MSG   112

typedef struct {
    uint32_t magic;
    uint32_t up_ms;
    char     msg[CRUMB_MSG];
} crumb_t;

static RTC_NOINIT_ATTR crumb_t s_crumb;

static void crumb_append(void *opaque, const char *m)
{
    (void)opaque;
    size_t n = strlen(s_crumb.msg);
    if (n >= CRUMB_MSG - 1) return;
    strncpy(s_crumb.msg + n, m, CRUMB_MSG - 1 - n);
    s_crumb.msg[CRUMB_MSG - 1] = '\0';
}

void esp_task_wdt_isr_user_handler(void)
{
    s_crumb.msg[0] = '\0';
    int cpus_fail = 0;
    esp_task_wdt_print_triggered_tasks(crumb_append, NULL, &cpus_fail);
    s_crumb.up_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_crumb.magic = CRUMB_MAGIC;
}

static void nvs_put(const char *s)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_str(h, KEY, s) == ESP_OK) nvs_commit(h);
    nvs_close(h);
}

void wdt_crumb_boot(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    bool abnormal = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
                    r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
                    r == ESP_RST_BROWNOUT || r == ESP_RST_CPU_LOCKUP;
    bool have = s_crumb.magic == CRUMB_MAGIC;
    if (!abnormal && !have) return;

    char line[CRUMB_MSG + 48];
    if (have)
        snprintf(line, sizeof line, "rst=%d up=%lums twdt:%s", (int)r,
                 (unsigned long)s_crumb.up_ms, s_crumb.msg);
    else
        snprintf(line, sizeof line, "rst=%d (no TWDT detail)", (int)r);
    s_crumb.magic = 0;
    nvs_put(line);
    ESP_LOGW(TAG, "last abnormal reset: %s", line);
}

static int cmd_wdt_last(int argc, char **argv)
{
    (void)argc; (void)argv;
    nvs_handle_t h;
    char buf[CRUMB_MSG + 48] = "";
    size_t n = sizeof buf;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, KEY, buf, &n) != ESP_OK) buf[0] = '\0';
        nvs_close(h);
    }
    printf("wdt-last: %s\n", buf[0] ? buf : "(none recorded)");
    printf("this boot: rst=%d up=%lums\n", (int)esp_reset_reason(),
           (unsigned long)(esp_timer_get_time() / 1000));
    return 0;
}

void wdt_crumb_register_console(void)
{
    const esp_console_cmd_t c = {
        .command = "wdt-last",
        .help    = "Print the last watchdog/panic breadcrumb (task names, uptime)",
        .func    = cmd_wdt_last,
    };
    esp_console_cmd_register(&c);
}
