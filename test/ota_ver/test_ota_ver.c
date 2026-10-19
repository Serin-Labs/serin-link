/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ota_ver.h"

#define SOAK      30000
#define DEADLINE  600000

static void test_ordering(void) {
    assert(ota_ver_cmp("1.2.3", "1.2.2") > 0);
    assert(ota_ver_cmp("1.2.2", "1.2.3") < 0);
    assert(ota_ver_cmp("1.2.3", "1.2.3") == 0);
    assert(ota_ver_cmp("2.0.0", "1.9.9") > 0);
    assert(ota_ver_cmp("0.10.0", "0.9.9") > 0);
}

static void test_prerelease_detection(void) {
    assert(ota_ver_is_prerelease("0.1.9-beta.1"));
    assert(ota_ver_is_prerelease("v0.1.9-rc.12"));
    assert(!ota_ver_is_prerelease("0.1.8"));
    /* git-describe tails on bench builds are not a release channel */
    assert(!ota_ver_is_prerelease("0.1.8-3-gabc1234"));
    assert(!ota_ver_is_prerelease("0.1.8-dirty"));
    assert(!ota_ver_is_prerelease("0.1.8-beta.1-dirty"));
    assert(!ota_ver_is_prerelease("0.1.8-beta."));
    assert(!ota_ver_is_prerelease("garbage"));
}

static void test_unparseable_reads_equal(void) {
    assert(ota_ver_cmp("garbage", "1.0.0") == 0);
    assert(ota_ver_cmp("1.0.0", "garbage") == 0);
    assert(ota_ver_cmp("1.2", "1.0.0") == 0);
}

static void test_v_prefix_tolerated(void) {
    assert(ota_ver_cmp("v1.2.3", "1.2.2") > 0);
    assert(ota_ver_cmp("1.2.3", "v1.2.2") > 0);
    assert(ota_ver_cmp("V1.2.3", "v1.2.3") == 0);

    assert(ota_ver_cmp("0.1.7-beta.1", "0.1.6") > 0);
    assert(ota_ver_cmp("0.1.7", "0.1.7-beta.3") > 0);
    assert(ota_ver_cmp("0.1.7-beta.3", "0.1.7") < 0);
    assert(ota_ver_cmp("0.1.7-beta.2", "0.1.7-beta.1") > 0);
    assert(ota_ver_cmp("0.1.7-beta.1", "0.1.7-beta.2") < 0);
    assert(ota_ver_cmp("0.1.7-beta.1", "0.1.7-beta.1") == 0);
    assert(ota_ver_cmp("0.1.7-beta.10", "0.1.7-beta.9") > 0);
    assert(ota_ver_cmp("0.1.8-beta.1", "0.1.7") > 0);
    assert(ota_ver_cmp("0.1.7-beta", "0.1.7-beta.1") < 0);
    assert(ota_ver_cmp("0.1.7-rc.1", "0.1.7-beta.1") == 0);
    assert(ota_ver_cmp("v0.1.7-beta.1", "0.1.6") > 0);
}

static void test_large_numeric_components_do_not_overflow(void) {
    /* The publisher permits up to 31 bytes, with no machine-integer limit. */
    assert(ota_ver_cmp("2147483648.0.0", "0.2.0") > 0);
    assert(ota_ver_cmp("0.2147483648.0", "0.2147483647.0") > 0);
    assert(ota_ver_cmp("0.0.4294967296", "0.0.4294967295") > 0);
    assert(ota_ver_cmp("999999999999999999999999999.0.0", "999999999999999999999999998.0.0") > 0);
    assert(ota_ver_cmp("999999999999999999999999998.0.0", "999999999999999999999999999.0.0") < 0);
    assert(ota_ver_cmp("0.3.0-beta.2147483648", "0.3.0-beta.2147483647") > 0);
    assert(ota_ver_cmp("0.3.0-beta.9999999999999999999999", "0.3.0") < 0);
    assert(ota_ver_cmp("000000000000000000000000001.0.0", "1.0.0") == 0);
    assert(ota_ver_is_prerelease("0.3.0-beta.9999999999999999999999"));
}

static void test_release_version_stays_inside_the_descriptor(void) {
    assert(ota_ver_is_release("999999999999999999999999999.0.0"));
    assert(!ota_ver_is_release("9999999999999999999999999999.0.0"));
    /* A heap allocation also makes a read past the field visible to ASan. */
    char *unterminated = malloc(OTA_VERSION_LEN);
    assert(unterminated);
    memset(unterminated, '9', OTA_VERSION_LEN);
    assert(!ota_ver_is_release(unterminated));
    free(unterminated);
}

static void test_hexval(void) {
    assert(ota_hexval('0') == 0  && ota_hexval('9') == 9);
    assert(ota_hexval('a') == 10 && ota_hexval('f') == 15);
    assert(ota_hexval('A') == 10 && ota_hexval('F') == 15);
    assert(ota_hexval('g') < 0 && ota_hexval(' ') < 0 && ota_hexval('\0') < 0);
}

static void test_guard_waits_out_the_soak(void) {
    ota_guard_t guard = {.healthy_since_ms = -1};
    for (int64_t t = 0; t <= 28000; t += 2000)
        assert(ota_guard_decide(&guard, t, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(&guard, 29999, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(&guard, 30000, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_VALID);
}

static void test_guard_never_marks_an_unhealthy_image_valid(void) {
    ota_guard_t guard = {.healthy_since_ms = -1};
    assert(ota_guard_decide(&guard, 30000, false, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(&guard, 599999, false, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
}

static void test_guard_rolls_back_at_the_deadline(void) {
    ota_guard_t guard = {.healthy_since_ms = -1};
    assert(ota_guard_decide(&guard, 600000, false, true, SOAK, DEADLINE, 4000) == OTA_GUARD_ROLLBACK);
    assert(ota_guard_decide(&guard, 9999999, false, true, SOAK, DEADLINE, 4000) == OTA_GUARD_ROLLBACK);
}

static void test_guard_deadline_beats_health(void) {
    ota_guard_t guard = {.healthy_since_ms = 570000, .last_poll_ms = 598000};
    assert(ota_guard_decide(&guard, 600000, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_ROLLBACK);
}

static void test_guard_poll_gap_boundary(void) {
    ota_guard_t guard = {.healthy_since_ms = -1};
    assert(ota_guard_decide(&guard, 0, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(&guard, 4000, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
    assert(guard.healthy_since_ms == 0);
    assert(ota_guard_decide(&guard, 8001, true, true, SOAK, DEADLINE, 4000) == OTA_GUARD_WAIT);
    assert(guard.healthy_since_ms == 8001);
}

static void test_board_check_needs_the_same_project_name(void) {
    const char *run15 = "serin_link_viewe15";
    assert(ota_same_board(run15, "serin_link_viewe15"));
    assert(!ota_same_board(run15, "serin_link_viewe21"));
    assert(!ota_same_board("serin_link_viewe21", run15));
    /* Images built before the per-board name all carry the shared one; the
       manifest's board id already picked them, so they still install. */
    assert(ota_same_board(run15, "serin_link"));
    assert(ota_same_board("serin_link_viewe21", "serin_link"));
    assert(!ota_same_board(run15, "serin_lin"));
    assert(!ota_same_board(run15, "serin_link_"));
    assert(!ota_same_board(run15, "serin_link_viewe"));
    assert(!ota_same_board(run15, ""));
    assert(!ota_same_board("", ""));
    /* esp_app_desc_t.project_name is char[32]; a downloaded one may lack its NUL. */
    char a[OTA_PROJECT_NAME_LEN], b[OTA_PROJECT_NAME_LEN + 1];
    memset(a, 'x', sizeof a);
    memset(b, 'x', sizeof b);
    assert(ota_same_board(a, b));
    a[OTA_PROJECT_NAME_LEN - 1] = 'y';
    assert(!ota_same_board(a, b));
}

static void test_build_time_reads_the_app_descriptor_strings(void) {
    /* esp_app_desc_t.date/.time are __DATE__/__TIME__, read as UTC. */
    assert(ota_build_epoch("Oct 12 2026", "11:08:42") == 1791803322LL);
    assert(ota_build_epoch("Jan  2 2026", "03:04:05") == 1767323045LL);   /* space-padded day */
    assert(ota_build_epoch("Feb 29 2028", "23:59:59") == 1835481599LL);
    assert(ota_build_epoch("Mar  1 2028", "00:00:00") == 1835481600LL);
    assert(ota_build_epoch("Dec 31 2026", "23:59:59") == 1798761599LL);
    assert(ota_build_epoch("Jan  1 1970", "00:00:00") == 0);
    /* CONFIG_APP_COMPILE_TIME_DATE off leaves both empty: no build time. */
    assert(ota_build_epoch("", "") < 0);
    assert(ota_build_epoch("Oct 12 2026", "") < 0);
    assert(ota_build_epoch("Foo 12 2026", "11:08:42") < 0);
    assert(ota_build_epoch("anF 12 2026", "11:08:42") < 0);
    assert(ota_build_epoch("Oct 32 2026", "11:08:42") < 0);
    assert(ota_build_epoch("Oct 12 2026", "24:00:00") < 0);
}

static void test_clock_floor_is_the_build_time(void) {
    const int64_t build = 1791803322LL;
    assert(ota_clock_floor(0, build) == build);              /* never set: 1970 */
    assert(ota_clock_floor(build - 1, build) == build);
    assert(ota_clock_floor(build, build) == build);
    assert(ota_clock_floor(build + 5, build) == build + 5);  /* a later clock stays */
    assert(ota_clock_floor(123, -1) == 123);                 /* no build time: untouched */
}

int main(void) {
    test_prerelease_detection();
    test_ordering();
    test_unparseable_reads_equal();
    test_v_prefix_tolerated();
    test_large_numeric_components_do_not_overflow();
    test_release_version_stays_inside_the_descriptor();
    test_hexval();
    test_guard_waits_out_the_soak();
    test_guard_never_marks_an_unhealthy_image_valid();
    test_guard_rolls_back_at_the_deadline();
    test_guard_deadline_beats_health();
    test_guard_poll_gap_boundary();
    test_board_check_needs_the_same_project_name();
    test_build_time_reads_the_app_descriptor_strings();
    test_clock_floor_is_the_build_time();
    printf("ota_ver: all tests passed\n");
    return 0;
}
