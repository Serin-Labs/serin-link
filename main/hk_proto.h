/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <string.h>

typedef enum { HK_PROTO_UNKNOWN = 0, HK_PROTO_HOMEKIT, HK_PROTO_MATTER } hk_proto_t;

static inline hk_proto_t hk_proto_from(const char *payload, const char *code)
{
    if (payload && strncmp(payload, "MT:", 3) == 0)  return HK_PROTO_MATTER;
    if (payload && strncmp(payload, "X-HM", 4) == 0) return HK_PROTO_HOMEKIT;

    if (code && code[0]) {
        size_t n = strlen(code);
        if (n == 10 && code[3] == '-' && code[6] == '-') return HK_PROTO_HOMEKIT;
        if (n == 11) {
            size_t digits = 0;
            for (const char *p = code; *p; p++)
                if (*p >= '0' && *p <= '9') digits++;
            if (digits == 11) return HK_PROTO_MATTER;
        }
    }
    return HK_PROTO_UNKNOWN;
}
