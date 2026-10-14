/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#include <assert.h>
#include <stdio.h>
#include "child_lock.h"

/* An engaged lock gates the live face, both offline faces, and an offline
 * face dismissed to the remind state. */
static const child_lock_face_t GATED[] = {
    CHILD_LOCK_FACE_LIVE, CHILD_LOCK_FACE_LINK_OFF,
    CHILD_LOCK_FACE_CN105_OFF, CHILD_LOCK_FACE_REMIND,
};
#define N_GATED (sizeof GATED / sizeof *GATED)

static void test_lock_gates_live_offline_and_remind_faces(void)
{
    child_lock_t l;
    child_lock_init(&l, true);
    for (size_t i = 0; i < N_GATED; i++) assert(child_lock_gates(&l, GATED[i]));
}

/* First-setup exemption: a dial with no bond shows pairing, and a bonded dial
 * whose controller has no Wi-Fi shows the
 * Wi-Fi offer. Both stay reachable while the lock is engaged. */
static void test_lock_exempts_unpaired_setup_face(void)
{
    child_lock_t l;
    child_lock_init(&l, true);
    assert(!child_lock_gates(&l, CHILD_LOCK_FACE_UNPAIRED));
}

static void test_lock_exempts_wifi_off_setup_face(void)
{
    child_lock_t l;
    child_lock_init(&l, true);
    assert(!child_lock_gates(&l, CHILD_LOCK_FACE_WIFI_OFF));
}

static void test_unlock_window_and_disabled_lock_gate_no_face(void)
{
    child_lock_t l;
    child_lock_init(&l, true);
    child_lock_open_window(&l);
    for (size_t i = 0; i < N_GATED; i++) assert(!child_lock_gates(&l, GATED[i]));
    child_lock_note_idle(&l);
    for (size_t i = 0; i < N_GATED; i++) assert(child_lock_gates(&l, GATED[i]));
    child_lock_init(&l, false);
    for (size_t i = 0; i < N_GATED; i++) assert(!child_lock_gates(&l, GATED[i]));
}

int main(void)
{
    child_lock_t l;

    child_lock_init(&l, false);
    assert(!child_lock_engaged(&l));

    child_lock_init(&l, true);
    assert(child_lock_engaged(&l));

    child_lock_open_window(&l);
    assert(!child_lock_engaged(&l));
    child_lock_note_idle(&l);
    assert(child_lock_engaged(&l));

    child_lock_init(&l, false);
    child_lock_open_window(&l);
    assert(!l.window_open);
    child_lock_init(&l, true);
    child_lock_open_window(&l);
    child_lock_disarm(&l);
    assert(!child_lock_engaged(&l) && !l.enabled && !l.window_open);

    test_lock_gates_live_offline_and_remind_faces();
    test_lock_exempts_unpaired_setup_face();
    test_lock_exempts_wifi_off_setup_face();
    test_unlock_window_and_disabled_lock_gate_no_face();

    puts("child_lock: OK");
    return 0;
}
