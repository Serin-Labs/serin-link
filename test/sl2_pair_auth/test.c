#include <assert.h>
#include <stdio.h>
#include "sl2_pair_auth.h"

int main(void) {
    const uint8_t dial[6] = {1, 2, 3, 4, 5, 6};
    const uint8_t ctrl[6] = {7, 8, 9, 10, 11, 12};
    uint8_t lmk[16] = {0}, previous_lmk[16] = {1};
    struct sl2_pair_auth_pkt ack, proof, previous;
    sl2_pair_auth_make(&ack, SL2_PKT_PAIR_ACK, lmk, dial, ctrl);
    sl2_pair_auth_make(&proof, SL2_PKT_PAIR_CONFIRM, lmk, dial, ctrl);
    sl2_pair_auth_make(&previous, SL2_PKT_PAIR_ACK, previous_lmk, dial, ctrl);
    /* Independent Python hashlib/hmac vector pins the byte transcript. */
    const uint8_t expected[32] = {
        0x7b, 0xbb, 0x86, 0xa2, 0x81, 0x97, 0x01, 0x50,
        0x0a, 0xa2, 0x63, 0x0d, 0xdb, 0xbf, 0x21, 0x17,
        0xa6, 0x68, 0x47, 0x94, 0xa7, 0x95, 0xb8, 0x9d,
        0x95, 0x2a, 0xf1, 0x48, 0xdb, 0xae, 0x54, 0x64,
    };
    assert(memcmp(ack.tag, expected, sizeof expected) == 0);
    assert(sl2_pair_auth_matches((uint8_t *)&ack, sizeof ack, &ack));
    /* Reflection and a valid ACK from the previous handshake cannot confirm. */
    assert(!sl2_pair_auth_matches((uint8_t *)&proof, sizeof proof, &ack));
    assert(!sl2_pair_auth_matches((uint8_t *)&previous, sizeof previous, &ack));
    for (int n = 0; n < SL2_PAIR_AUTH_MIN_LEN; n++)
        assert(!sl2_pair_auth_matches((uint8_t *)&ack, n, &ack));
    for (size_t n = 0; n < sizeof ack; n++) {
        previous = ack;
        ((uint8_t *)&previous)[n] ^= 1;
        assert(!sl2_pair_auth_matches((uint8_t *)&previous, sizeof previous, &ack));
    }
    sl2_pair_auth_make(&previous, SL2_PKT_PAIR_ACK, lmk, ctrl, dial);
    assert(!sl2_pair_auth_matches((uint8_t *)&previous, sizeof previous, &ack));
    /* Ordinary STATE, even long enough and bearing the expected tag bytes,
     * never proves this key. The production RX callback uses this predicate. */
    previous = ack;
    previous.type = SL2_PKT_STATE;
    assert(!sl2_pair_auth_matches((uint8_t *)&previous, sizeof previous, &ack));
    assert(!sl2_pair_auth_matches((uint8_t *)&previous, 2, &ack));
    puts("pair confirmation rejects stale STATE, previous keys, short/tampered frames and reflection");
    return 0;
}
