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
