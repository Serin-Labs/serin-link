/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include "hk_proto.h"

int main(void)
{
    assert(hk_proto_from("MT:Y.K9042C00KA0648G00", "")           == HK_PROTO_MATTER);
    assert(hk_proto_from("X-HM://0023ABCDE", "")                 == HK_PROTO_HOMEKIT);
    assert(hk_proto_from("MT:Y.K9042C00KA0648G00", "123-45-678") == HK_PROTO_MATTER);

    assert(hk_proto_from("", "123-45-678")  == HK_PROTO_HOMEKIT);
    assert(hk_proto_from("", "34970112332") == HK_PROTO_MATTER);
    assert(hk_proto_from(NULL, "123-45-678") == HK_PROTO_HOMEKIT);

    assert(hk_proto_from("", "")      == HK_PROTO_UNKNOWN);
    assert(hk_proto_from(NULL, NULL)  == HK_PROTO_UNKNOWN);
    assert(hk_proto_from("", "---")   == HK_PROTO_UNKNOWN);
    assert(hk_proto_from("", "12345") == HK_PROTO_UNKNOWN);
    assert(hk_proto_from("", "1234567890a") == HK_PROTO_UNKNOWN);

    printf("hk_proto: all tests passed\n");
    return 0;
}
