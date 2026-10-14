/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline int ota_pre_num(const char *s)
{
    const char *dash = strchr(s, '-');
    if (!dash) return -1;
    const char *dot = strrchr(dash, '.');
    int n = 0;
    if (dot && sscanf(dot + 1, "%d", &n) == 1) return n;
    return 0;
}

static inline int ota_ver_cmp(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    int x[3] = {0}, y[3] = {0};
    if (sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]) != 3) return 0;
    if (sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]) != 3) return 0;
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] - y[i];
    int pa = ota_pre_num(a), pb = ota_pre_num(b);
    if (pa == pb) return 0;
    if (pa < 0) return 1;
    if (pb < 0) return -1;
    return pa - pb;
}

/* A release-channel pre-release is exactly maj.min.patch-word.N. A git-describe
   tail on a bench build (-3-gabc1234, -dirty) is not one. */
static inline bool ota_ver_is_prerelease(const char *v)
{
    if (*v == 'v' || *v == 'V') v++;
    int x[3], n = 0;
    if (sscanf(v, "%d.%d.%d-%n", &x[0], &x[1], &x[2], &n) != 3 || n == 0) return false;
    const char *p = v + n;
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) p++;
    if (*p++ != '.' || *p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';
}

static inline int ota_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

typedef enum {
    OTA_GUARD_WAIT = 0,
    OTA_GUARD_VALID,
    OTA_GUARD_ROLLBACK,
} ota_guard_act_t;

static inline ota_guard_act_t ota_guard_decide(int64_t up_ms, bool healthy,
                                               int64_t soak_ms, int64_t deadline_ms)
{
    if (healthy && up_ms >= soak_ms) return OTA_GUARD_VALID;
    if (up_ms >= deadline_ms)        return OTA_GUARD_ROLLBACK;
    return OTA_GUARD_WAIT;
}

/* esp_app_desc_t.project_name. The build stamps the board into it
   (main/project_include.cmake), and both boards share one image format, so the
   name is the only thing that tells a 1.5" image from a 2.1" one. A downloaded
   descriptor is untrusted and may fill all 32 bytes without a NUL. */
#define OTA_PROJECT_NAME_LEN 32
/* Every image published before the per-board name carries this one. The
   manifest already picks the image by board id, so accepting it is no weaker
   than before the name check existed. */
#define OTA_LEGACY_PROJECT_NAME "serin_link"

static inline bool ota_same_board(const char *running, const char *incoming)
{
    if (incoming[0] == '\0') return false;
    if (strncmp(incoming, OTA_LEGACY_PROJECT_NAME, OTA_PROJECT_NAME_LEN) == 0) return true;
    return strncmp(running, incoming, OTA_PROJECT_NAME_LEN) == 0;
}

static inline int64_t ota_days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = (int)(y - era * 400);
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* The firmware's build time from esp_app_desc_t.date ("Oct 12 2026", the
   __DATE__ form) and .time ("11:08:42"), as Unix seconds. The strings carry no
   zone, so they are read as UTC; hours off do not matter for certificate
   dates. -1 when either string does not parse. */
static inline int64_t ota_build_epoch(const char *date, const char *hms)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int d, y, h, mi, s;
    if (sscanf(date, "%3s %d %d", mon, &d, &y) != 3) return -1;
    if (sscanf(hms, "%d:%d:%d", &h, &mi, &s) != 3) return -1;
    const char *at = strlen(mon) == 3 ? strstr(months, mon) : NULL;
    if (!at || (at - months) % 3) return -1;
    if (d < 1 || d > 31 || y < 1970 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60)
        return -1;
    return ota_days_from_civil(y, (int)(at - months) / 3 + 1, d) * 86400 + h * 3600 + mi * 60 + s;
}

/* With no SNTP time, certificate dates are checked against the build time
   instead of an unset clock: a clock earlier than the build is wrong for sure.
   A later clock, set by an earlier sync, is kept. */
static inline int64_t ota_clock_floor(int64_t now, int64_t build)
{
    return (build >= 0 && now < build) ? build : now;
}
