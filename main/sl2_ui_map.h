/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Mehmet Bayram
 */
#pragma once
#include <string.h>
#include "sl2_proto.h"
#include "ui.h"

static inline hvac_mode_t mode_from_sl2(uint8_t m) {
    switch (m) {
        case SL2_MODE_OFF:       return MODE_OFF;
        case SL2_MODE_HEAT:      return MODE_HEAT;
        case SL2_MODE_COOL:      return MODE_COOL;
        case SL2_MODE_HEAT_COOL: return MODE_HEAT_COOL;
        case SL2_MODE_AUTO:      return MODE_AUTO;
        case SL2_MODE_DRY:       return MODE_DRY;
        case SL2_MODE_FAN_ONLY:  return MODE_FAN;
        default:                 return MODE_AUTO;
    }
}
static inline uint8_t mode_to_sl2(hvac_mode_t m) {
    switch (m) {
        case MODE_OFF:  return SL2_MODE_OFF;
        case MODE_HEAT: return SL2_MODE_HEAT;
        case MODE_COOL: return SL2_MODE_COOL;
        case MODE_HEAT_COOL: return SL2_MODE_HEAT_COOL;
        case MODE_AUTO: return SL2_MODE_AUTO;
        case MODE_DRY:  return SL2_MODE_DRY;
        case MODE_FAN:  return SL2_MODE_FAN_ONLY;
        default:        return SL2_MODE_AUTO;
    }
}

static inline bool sl2_ui_vane_clamps(uint8_t v, uint8_t split) {
    return v != SL2_VANE_SWING && v != split && v > SL2_UI_VANE_MAX_POS;
}

static inline vvane_t vvane_from_sl2(uint8_t v) {
    if (v == SL2_VANE_SWING) return VANE_SWING;
    if (v == SL2_VANE_AUTO)  return VANE_AUTO;
    if (sl2_ui_vane_clamps(v, 0)) v = SL2_UI_VANE_MAX_POS;
    return (vvane_t)(VANE_1 + (v - 1));
}
static inline uint8_t vvane_to_sl2(vvane_t v) {
    switch (v) {
        case VANE_1: return 1; case VANE_2: return 2; case VANE_3: return 3;
        case VANE_4: return 4; case VANE_5: return 5;
        case VANE_SWING: return SL2_VANE_SWING;
        default: return SL2_VANE_AUTO;
    }
}

static inline hvane_t hvane_from_sl2(uint8_t v, uint8_t cap) {
    if (v == SL2_VANE_SWING) return HVANE_SWING;
    if (v == SL2_VANE_AUTO)  return HVANE_CENTER;
    uint8_t split = sl2_vanecap_split_pos(cap);
    if (split && v == split) return HVANE_AUTO;
    if (sl2_ui_vane_clamps(v, split)) v = SL2_UI_VANE_MAX_POS;
    return (hvane_t)(HVANE_LEFT + (v - 1));
}
static inline uint8_t hvane_to_sl2(hvane_t v, uint8_t cap) {
    switch (v) {
        case HVANE_LEFT:   return 1; case HVANE_CLEFT:  return 2;
        case HVANE_CENTER: return 3; case HVANE_CRIGHT: return 4;
        case HVANE_RIGHT:  return 5;
        case HVANE_SWING:  return SL2_VANE_SWING;
        case HVANE_AUTO: {
            uint8_t split = sl2_vanecap_split_pos(cap);
            return split ? split : SL2_VANE_AUTO;
        }
        default: return 3;
    }
}

static inline const char *stage_str(uint8_t st) {
    switch (st) {
        case 0x00: return "IDLE";     case 0x01: return "LOW";
        case 0x02: return "GENTLE";   case 0x03: return "MEDIUM";
        case 0x04: return "MODERATE"; case 0x05: return "HIGH";
        case 0x06: return "DIFFUSE";  default:   return "?";
    }
}

static inline const char *action_substate_str(uint8_t a) {
    switch (a) {
        case SL2_ACT_DEFROST: return "DEFROST";
        case SL2_ACT_PREHEAT: return "PREHEAT";
        case SL2_ACT_STANDBY: return "STANDBY";
        default:              return "";
    }
}

static inline bool action_active(uint8_t a) {
    switch (a) {
        case SL2_ACT_HEATING: case SL2_ACT_COOLING: case SL2_ACT_DRYING:
        case SL2_ACT_FAN:     case SL2_ACT_DEFROST: case SL2_ACT_PREHEAT:
            return true;
        default:
            return false;
    }
}

static inline ui_screen_gate_t sl2_screen_gate_from_flags2(uint8_t flags2) {
    if (!(flags2 & SL2_SF2_SCREEN_CTL)) return UI_SCREEN_GATE_NONE;
    return (flags2 & SL2_SF2_SCREEN_OFF) ? UI_SCREEN_GATE_OFF : UI_SCREEN_GATE_ON;
}

static inline ui_screen_gate_t sl2_screen_gate_merge(ui_screen_gate_t a,
                                                     ui_screen_gate_t b) {
    if (a == UI_SCREEN_GATE_ON  || b == UI_SCREEN_GATE_ON)  return UI_SCREEN_GATE_ON;
    if (a == UI_SCREEN_GATE_OFF || b == UI_SCREEN_GATE_OFF) return UI_SCREEN_GATE_OFF;
    return UI_SCREEN_GATE_NONE;
}

static inline bool sl2_night_from_flags2(uint8_t flags2) {
    return (flags2 & SL2_SF2_NIGHT_CTL) && (flags2 & SL2_SF2_NIGHT);
}

static inline size_t sl2_ui_strnlen(const char *s, size_t n) {
    const char *nul = (const char *)memchr(s, 0, n);
    return nul ? (size_t)(nul - s) : n;
}

static inline void tlv_str2(const uint8_t *v, uint8_t l, size_t skip,
                            char *a, size_t an, char *b, size_t bn) {
    a[0] = '\0'; b[0] = '\0';
    if (l <= skip) return;
    const char *pp = (const char *)v + skip;
    size_t left = l - skip;
    size_t n1 = sl2_ui_strnlen(pp, left);
    size_t ca = n1 < an - 1 ? n1 : an - 1;
    memcpy(a, pp, ca); a[ca] = '\0';
    if (n1 + 1 >= left) return;
    pp += n1 + 1; left -= n1 + 1;
    size_t n2 = sl2_ui_strnlen(pp, left);
    size_t cb = n2 < bn - 1 ? n2 : bn - 1;
    memcpy(b, pp, cb); b[cb] = '\0';
}
