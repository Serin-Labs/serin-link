/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
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

static void test_hexval(void) {
    assert(ota_hexval('0') == 0  && ota_hexval('9') == 9);
    assert(ota_hexval('a') == 10 && ota_hexval('f') == 15);
    assert(ota_hexval('A') == 10 && ota_hexval('F') == 15);
    assert(ota_hexval('g') < 0 && ota_hexval(' ') < 0 && ota_hexval('\0') < 0);
}

static void test_guard_waits_out_the_soak(void) {
    assert(ota_guard_decide(0,     true, SOAK, DEADLINE) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(29999, true, SOAK, DEADLINE) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(30000, true, SOAK, DEADLINE) == OTA_GUARD_VALID);
}

static void test_guard_never_marks_an_unhealthy_image_valid(void) {
    assert(ota_guard_decide(30000,  false, SOAK, DEADLINE) == OTA_GUARD_WAIT);
    assert(ota_guard_decide(599999, false, SOAK, DEADLINE) == OTA_GUARD_WAIT);
}

static void test_guard_rolls_back_at_the_deadline(void) {
    assert(ota_guard_decide(600000,  false, SOAK, DEADLINE) == OTA_GUARD_ROLLBACK);
    assert(ota_guard_decide(9999999, false, SOAK, DEADLINE) == OTA_GUARD_ROLLBACK);
}

static void test_guard_health_beats_the_deadline(void) {
    assert(ota_guard_decide(600000,  true, SOAK, DEADLINE) == OTA_GUARD_VALID);
    assert(ota_guard_decide(9999999, true, SOAK, DEADLINE) == OTA_GUARD_VALID);
}

int main(void) {
    test_prerelease_detection();
    test_ordering();
    test_unparseable_reads_equal();
    test_v_prefix_tolerated();
    test_hexval();
    test_guard_waits_out_the_soak();
    test_guard_never_marks_an_unhealthy_image_valid();
    test_guard_rolls_back_at_the_deadline();
    test_guard_health_beats_the_deadline();
    printf("ota_ver: all tests passed\n");
    return 0;
}
