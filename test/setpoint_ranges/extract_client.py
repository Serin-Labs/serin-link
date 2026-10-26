# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Mehmet Bayram
"""Extract current production temperature/edit paths, not copies of their logic."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[2]
s = (root / 'main/espnow_client.c').read_text()
def function(signature):
    start = s.index(signature)
    return s[start:s.index('\n}', start) + 2]
functions = ['static int canonical_setpoint_dd(', 'static void ctrl_from(', 'static void cache_temperature_state(', 'static void restore_zone_ui(', 'static bool held_f_ambiguous(', 'static void cmd_put_temp(',
             'static void cmd_put_band(', 'static bool edits_pending(',
             'static bool pending_edit_tx(', 'static void resend_held_edits(int zi) {',
             'void espnow_client_on_local_edit(', 'void espnow_client_on_units_edit(',
             'static void refresh_caps_ui(']
e1 = s.index('    if (p->set_dc != SL2_DC_NA) {', s.index('static void apply_state('))
e2 = s.index('    s.fan_pct', e1)
echo = 'static hvac_state_t temperature_echo(const struct sl2_state_pkt *p, bool use_f, uint32_t now) { hvac_state_t s = local_state; bool is_active = true; int zi = 0;\n#define z (&s_z[0])\n' + s[e1:e2] + '\n#undef z\nreturn s;}\n'
if len(sys.argv) > 2 and sys.argv[2] == 'ui':
    p1 = s.index('        s_use_f = use_f;', e2)
    p2 = s.index('        ui_set_link_state', p1)
    echo = 'static void receive_temperature_state(const struct sl2_state_pkt *p, bool use_f) { hvac_state_t s; ui_get_state(&s); bool is_active = true; int zi = 0; uint32_t now = now_ms();\n#define z (&s_z[0])\n' + s[e1:e2] + '\ns.use_f = use_f;\n' + s[p1:p2] + '\n#undef z\n}\n'
else:
    ui = (root / 'main/ui.c').read_text()
    start = ui.index('static struct { bool valid, units_c; int set, lo, hi; } s_caps_intent;')
    end = ui.index('static bool         s_edit', start)
    d1 = ui.index('static inline float dial_deg_per_step(')
    d2 = ui.index('\n}', d1) + 2
    tokens = (root / 'main/serin_tokens.h').read_text()
    t1 = tokens.index('static inline float serin_temp_t(')
    t2 = tokens.index('\n}', t1) + 2
    echo = tokens[t1:t2] + '\n#define s_state local_state\n' + ui[start:end] + '\n#undef s_state\n' + ui[d1:d2] + '\n' + echo
Path(sys.argv[1]).write_text('\n'.join(function(x) for x in functions) + '\n' + echo)
