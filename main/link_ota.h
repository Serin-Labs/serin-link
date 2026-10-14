/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <stdbool.h>

/* After an update the new image is on probation (PENDING_VERIFY) until
   link_ota.c's guard marks it valid, 30 s after boot at the earliest. Any
   restart before then makes the bootloader bring the previous version back. */
#define LINK_OTA_SOAK_MSG "Keep the dial plugged in for 30 seconds"

/* Without SNTP, an update sets the wall clock to the build time (TLS date checks);
   until a real sync it is wrong, so nothing may read it as the time of day. */
void link_ota_init(void);

bool link_ota_soaking(void);
/* For actions that end in esp_restart(), once the UI exists: during the soak,
   show LINK_OTA_SOAK_MSG on the update face and return true (do not restart). */
bool link_ota_refuse_restart(void);

void link_ota_begin(void);
void link_ota_confirm(void);
void link_ota_cancel(void);

void link_ota_register_console(void);
