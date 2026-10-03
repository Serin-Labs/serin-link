#include <assert.h>
#include <stdio.h>
#include "wifi_setup_session.h"
int main(void) {
    wifi_setup_session_t s = {0};
    uint32_t wire=0, ui=0;
    assert(wifi_setup_session_tx(&s, false, 1, 42, &wire) && wire==42);
    assert(wifi_setup_session_tx(&s, false, 1, 99, &wire) && wire==42);
    assert(!wifi_setup_session_ack(&s, 42, &ui));
    assert(wifi_setup_session_tx(&s, true, 1, 99, &wire) && wire==42);
    assert(!wifi_setup_session_tx(&s, false, 1, 99, &wire));
    assert(wifi_setup_session_ack(&s, 42, &ui) && ui==1);
    assert(wifi_setup_session_tx(&s, false, 2, 42, &wire) && wire!=42);
    assert(!wifi_setup_session_ack(&s, 42, &ui));
    assert(!wifi_setup_session_tx(&s, true, 1, 90, &wire));
    s=(wifi_setup_session_t){0};
    assert(wifi_setup_session_tx(&s, true, 1, 0, &wire) && wire!=0);
    assert(!wifi_setup_session_tx(&s, false, 1, 90, &wire));
    assert(!wifi_setup_session_tx(&s, true, 0, 90, &wire));
    s=(wifi_setup_session_t){.ui=UINT32_MAX,.wire=UINT32_MAX};
    assert(wifi_setup_session_tx(&s, false, 1, UINT32_MAX, &wire) && wire==1);
    puts("Wi-Fi session retries, stale replies, cancel-before-start and wrap passed");
}
