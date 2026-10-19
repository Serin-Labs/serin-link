// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <stdint.h>
int sim_cmd_exec(const char *line);          /* one command; 0 = ok */
int sim_cmd_run_string(const char *cmds);    /* "cmd; cmd; …" stops at first error */
int sim_cmd_run_script(const char *path);    /* one command per line; '#' comments */
void sim_cmd_on_wifi_setup(void);            /* ui_set_on_wifi_setup stub: counts + logs
                                              * (fires show up as `wz_req=` in info) */

void sim_wifi_session_start(uint32_t session);
void sim_wifi_cancel(uint32_t session);
