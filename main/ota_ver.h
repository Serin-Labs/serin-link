/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* esp_app_desc_t.version includes its NUL in these 32 bytes. */
#define OTA_VERSION_LEN 32

/* Return the suffix after three unsigned decimal components. Callers that
   accept a running build's v/V prefix strip it first. */
static inline const char *ota_ver_core_end(const char *v)
{
    for (int i = 0; i < 3; i++) {
        size_t n = strspn(v, "0123456789");
        if (!n) return NULL;
        v += n;
        if (i < 2 && *v++ != '.') return NULL;
    }
    return v;
}

/* Compare decimal spans without converting to a machine integer. Published
   versions can fill the descriptor even when a component exceeds INT_MAX. */
static inline int ota_ver_num_cmp(const char *a, const char *b)
{
    while (*a == '0') a++;
    while (*b == '0') b++;
    size_t an = strspn(a, "0123456789"), bn = strspn(b, "0123456789");
    if (an != bn) return an > bn ? 1 : -1;
    return strncmp(a, b, an);
}

static inline int ota_ver_cmp(const char *a, const char *b)
{
    if (*a == 'v' || *a == 'V') a++;
    if (*b == 'v' || *b == 'V') b++;
    if (!ota_ver_core_end(a) || !ota_ver_core_end(b)) return 0;
    for (int i = 0; i < 3; i++) {
        int cmp = ota_ver_num_cmp(a, b);
        if (cmp) return cmp;
        a += strspn(a, "0123456789");
        b += strspn(b, "0123456789");
        if (i < 2) { a++; b++; }
    }
    const char *pa = strchr(a, '-'), *pb = strchr(b, '-');
    if (!pa || !pb) return (pb != NULL) - (pa != NULL);
    pa = strrchr(pa, '.');
    pb = strrchr(pb, '.');
    /* Keep existing prerelease ordering: the final number determines order,
       regardless of label; a bench tail with no number reads as zero. */
    return ota_ver_num_cmp(pa ? pa + 1 : "0", pb ? pb + 1 : "0");
}

/* A release-channel pre-release is exactly maj.min.patch-word.N. A git-describe
   tail on a bench build (-3-gabc1234, -dirty) is not one. */
static inline bool ota_ver_is_prerelease(const char *v)
{
    if (*v == 'v' || *v == 'V') v++;
    const char *p = ota_ver_core_end(v);
    if (!p || *p++ != '-') return false;
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) p++;
    if (*p++ != '.' || *p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
    return *p == '\0';
}

/* The publisher's exact grammar: maj.min.patch[-word.N], at most 31 ASCII
   bytes. Do not accept v/V or other aliases for an incoming release. Check
   termination before any parser reads the downloaded descriptor as a string. */
static inline bool ota_ver_is_release(const char *v)
{
    if (!v) return false;
    size_t n = 0;
    while (n < OTA_VERSION_LEN && v[n]) n++;
    if (n == OTA_VERSION_LEN) return false;
    const char *end = ota_ver_core_end(v);
    return end && (*end == '\0' || ota_ver_is_prerelease(v));
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

typedef struct {
    int64_t healthy_since_ms; /* Initialize to -1: no healthy observation yet. */
    int64_t last_poll_ms;
} ota_guard_t;

/* Link health is sampled, not a guarantee between polls. A missed observation
   window starts a new streak. rendered is sticky evidence of one UI frame,
   independent of the link streak; it does not prove ongoing UI health. */
static inline ota_guard_act_t ota_guard_decide(ota_guard_t *guard, int64_t up_ms,
                                               bool link, bool rendered, int64_t soak_ms,
                                               int64_t deadline_ms, int64_t max_poll_gap_ms)
{
    if (up_ms >= deadline_ms) return OTA_GUARD_ROLLBACK;
    if (!link || up_ms - guard->last_poll_ms > max_poll_gap_ms)
        guard->healthy_since_ms = -1;
    guard->last_poll_ms = up_ms;
    if (link) {
        if (guard->healthy_since_ms < 0) guard->healthy_since_ms = up_ms;
        if (rendered && up_ms - guard->healthy_since_ms >= soak_ms)
            return OTA_GUARD_VALID;
    }
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
