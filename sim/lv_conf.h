// SPDX-License-Identifier: GPL-3.0-only
/* lv_conf.h — LVGL config for the PC simulator. Mirrors device-relevant
 * Kconfig choices; anything unset falls back to LVGL defaults through
 * lv_conf_internal.h. Included via -DLV_CONF_INCLUDE_SIMPLE. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16              /* device parity (RGB565) */

/* device parity: clib malloc/string/sprintf */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE           /* single-threaded sim */

/* sim I/O */
#define LV_USE_SDL      1
#define LV_USE_SNAPSHOT 1

/* Device parity (sdkconfig.defaults): no theme, no default-font reliance, and
 * only the widget classes the UI instantiates. Pixel-identical goldens across
 * this change are the proof that nothing leaned on the theme or on
 * LV_FONT_DEFAULT; keep the two lists in step or that proof is void. */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE  0
#define LV_USE_THEME_MONO    0   /* lv_conf_internal falls through to MONO=1 otherwise — white arcs */
#define LV_FONT_UNSCII_8     1
#define LV_FONT_DEFAULT      &lv_font_unscii_8
#define LV_USE_QRCODE 1
#define LV_USE_GRID 0
#define LV_USE_ANIMIMG 0
#define LV_USE_ARCLABEL 0
#define LV_USE_BUTTON 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#endif /* LV_CONF_H */
