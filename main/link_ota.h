/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once

void link_ota_init(void);

void link_ota_begin(void);
void link_ota_confirm(void);
void link_ota_cancel(void);

void link_ota_register_console(void);
