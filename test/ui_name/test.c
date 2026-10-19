/* SPDX-License-Identifier: GPL-3.0-only */
#include "ui_name.h"
#include <assert.h>
#include <string.h>

int main(void)
{
    char out[32];
    ui_name_copy(out, sizeof out, "Küche", sizeof "Küche");
    assert(strcmp(out, "Küche") == 0);
    ui_name_copy(out, 4, "ABCDEF", 6);
    assert(strcmp(out, "ABC") == 0);
    ui_name_copy(out, 4, "Aü", 3);
    assert(strcmp(out, "Aü") == 0);
    ui_name_copy(out, 3, "Aü", 3);
    assert(strcmp(out, "A") == 0);
    const char unterminated[] = {'A', (char)0xc3};
    ui_name_copy(out, sizeof out, unterminated, sizeof unterminated);
    assert(strcmp(out, "A") == 0);
    /* Invalid encodings stop at their first byte, without replacement glyphs. */
    const char *invalid[] = {"A\x80", "A\xc0\xaf", "A\xe0\x80\x80",
                            "A\xed\xa0\x80", "A\xf4\x90\x80\x80",
                            "A\xf5\x80\x80\x80", "A\xc3X"};
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        ui_name_copy(out, sizeof out, invalid[i], strlen(invalid[i]));
        assert(strcmp(out, "A") == 0);
    }
    ui_name_copy(out, sizeof out, "A€😀", sizeof "A€😀");
    assert(strcmp(out, "A€😀") == 0); /* Copying UTF-8 does not promise font coverage. */
    ui_name_copy(out, 7, "A€😀", sizeof "A€😀");
    assert(strcmp(out, "A€") == 0);
    strcpy(out, "Aü");
    ui_name_copy(out, 3, out, sizeof out);
    assert(strcmp(out, "A") == 0);
    out[0] = 'X';
    ui_name_copy(out, 0, "A", 1);
    assert(out[0] == 'X');
    ui_name_copy(out, 1, "A", 1);
    assert(out[0] == '\0');
    ui_name_copy(out, sizeof out, NULL, 0);
    assert(out[0] == '\0');
    return 0;
}
