/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stddef.h>
#include <string.h>

/* Copy the valid UTF-8 prefix of a bounded controller name, reserving a NUL.
 * Stop before a malformed or truncated sequence. Source need not end in NUL;
 * in-place use is supported. Glyph coverage is a separate font constraint. */
static inline void ui_name_copy(char *dst, size_t capacity,
                                const char *src, size_t source_size)
{
    if (!capacity) return;
    size_t limit = source_size < capacity - 1 ? source_size : capacity - 1;
    size_t end = 0;
    while (src && end < limit && src[end]) {
        unsigned char first = (unsigned char)src[end];
        size_t width;
        if (first < 0x80) width = 1;
        else if (first >= 0xc2 && first <= 0xdf) width = 2;
        else if (first >= 0xe0 && first <= 0xef) width = 3;
        else if (first >= 0xf0 && first <= 0xf4) width = 4;
        else break;
        if (width > limit - end) break;
        size_t i = 1;
        for (; i < width; i++) {
            unsigned char byte = (unsigned char)src[end + i];
            if (byte < 0x80 || byte > 0xbf) break;
        }
        if (i != width) break;
        if (width >= 3) {
            unsigned char second = (unsigned char)src[end + 1];
            if ((first == 0xe0 && second < 0xa0) ||
                (first == 0xed && second >= 0xa0) ||
                (first == 0xf0 && second < 0x90) ||
                (first == 0xf4 && second >= 0x90)) break;
        }
        end += width;
    }
    if (end) memmove(dst, src, end);
    dst[end] = '\0';
}
