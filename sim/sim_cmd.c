// SPDX-License-Identifier: GPL-3.0-only
/*
 * sim_cmd.c — the simulator command language. One parser, three entry
 * points (stdin, --run, --script). Grammar (tokens split on whitespace):
 *   state <field>=<val> ...   any hvac_state_t field; enums by name or number
 *   caps <key>=<val> ...      ui_caps_t snapshot: modes features fan_steps
 *                             fan_auto vane_v vane_h min_dc max_dc step_dc ftab
 *                             (partial keys mutate a persistent full default)
 *   screen <name>             thermostat fanvane menu mode settings update about wifi zones
 *                             advanced room_source
 *   bootface <pct>            build the boot-gesture reset countdown face (main/
 *                             factory_reset.c) on the active screen at pct fill
 *   turn <n>                  n encoder detents, negative = CCW
 *   press | long | down | up  knob button (long = down, 750ms, hold, up)
 *   pair <st> [ch]            hidden prompt searching ok fail full
 *   coach arm                 clear the first-run coach's seen flag + re-arm its trigger
 *   ota <st> [pct] [txt]      hidden checking confirm download verify ok uptodate fail
 *   bridge <b> <o> <rssi>     bonded/online 0|1
 *   link <pv> <age_s>         stub peer proto version + last-rx age
 *   fw <ver> <build>          pin the About page's version/build strings. sim_main
 *                             seeds the build from __DATE__ (a COMPILE-time constant),
 *                             so About's pixels move every time the sim is rebuilt on
 *                             a new day. Pin both strings for comparable captures.
 *   zones <n> [active]        n fake zones (0..7), active index
 *   zone <i> k=v ...          name=/badge=/temp=/mode=/status=/set=/lo=/hi=/state= on fake zone i
 *   roomsrc <sel> <a,b,c> [st]  fake room-source catalog + applied selection
 *   tap <x> <y> | swipe <x0> <y0> <x1> <y1> <ms>   synthetic touch (headless)
 *   info                       one-line UI state readback
 *   hitmap                     true hit box of every visible clickable (SERIN_TOUCH_MIN check)
 *   hitat <x> <y>              which object a click at (x,y) reaches (z-order aware)
 *   labelbox <text>            drawn box of the label matching <text> exactly (any object,
 *                              clickable or not — for hit-test math keyed off a plain child)
 *   tree                       every VISIBLE object: class + drawn box (+ text, for labels).
 *                              The structural fingerprint of a face — what is on screen,
 *                              independent of animation phase. Comparing trees can reveal
 *                              widgets left over from the previous face without pixel
 *                              differences caused by an animation phase.
 *   labels                     every visible label's text, one per line (screen + top layer),
 *                              newlines escaped — the COPY assertion: grep it for what the
 *                              dial is actually saying (labelbox matches one token only)
 *   fontcheck                  every visible label's chars vs its own font's glyphs —
 *                              names any that would render as a placeholder box
 *                              (tofu). The faces are subset-generated, so this is
 *                              the guard on copy edits and FONT_* repoints. Returns
 *                              failure if any glyph is missing.
 *   wait <ms> | shot <file> | quit
 */
#include "sim_cmd.h"
#include "sim.h"
#include "sim_time.h"
#include "sim_shot.h"
#include "ui.h"
#include "ui_internal.h"                    /* s_brightness / apply_brightness for `bright` */
#include "sl2_proto.h"                      /* room-source catalog entries for `roomsrc` */
#include "lvgl.h"
#include "src/misc/lv_area_private.h"       /* lv_area_intersect / lv_area_is_point_on */
#include "src/core/lv_obj_draw_private.h"   /* lv_obj_get_ext_draw_size */
#include "src/misc/lv_text_private.h"       /* lv_text_encoded_next — UTF-8 → codepoint */
#include "serin_tokens.h"
#include "ui_fonts_dial.h"                  /* the faces, by name, for fontcheck */
#include "factory_reset.h"                  /* factory_reset_face_build — the boot-countdown face */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

typedef struct { const char *name; int val; } nv_t;
static const nv_t T_MODE[]  = {{"off",MODE_OFF},{"heat",MODE_HEAT},{"cool",MODE_COOL},
                               {"heat_cool",MODE_HEAT_COOL},{"auto",MODE_AUTO},
                               {"dry",MODE_DRY},{"fan",MODE_FAN},{0,0}};
static const nv_t T_STAT[]  = {{"off",ST_OFF},{"idle",ST_IDLE},{"active",ST_ACTIVE},{0,0}};
static const nv_t T_VV[]    = {{"auto",VANE_AUTO},{"1",VANE_1},{"2",VANE_2},{"3",VANE_3},
                               {"4",VANE_4},{"5",VANE_5},{"swing",VANE_SWING},{0,0}};
static const nv_t T_HV[]    = {{"auto",HVANE_AUTO},{"left",HVANE_LEFT},{"cleft",HVANE_CLEFT},
                               {"center",HVANE_CENTER},{"cright",HVANE_CRIGHT},
                               {"right",HVANE_RIGHT},{"swing",HVANE_SWING},{0,0}};
static const nv_t T_ALERT[] = {{"none",ALERT_NONE},{"nolink",ALERT_NOLINK},
                               {"pending",ALERT_PENDING},{"fault",ALERT_FAULT},
                               {"offline",ALERT_OFFLINE},{"syncing",ALERT_SYNCING},{0,0}};
static const nv_t T_PAIR[]  = {{"hidden",PAIR_UI_HIDDEN},{"prompt",PAIR_UI_PROMPT},
                               {"searching",PAIR_UI_SEARCHING},{"ok",PAIR_UI_OK},
                               {"fail",PAIR_UI_FAIL},{"full",PAIR_UI_FULL},
                               {"mismatch",PAIR_UI_MISMATCH},{0,0}};
static const nv_t T_OTA[]   = {{"hidden",OTA_UI_HIDDEN},{"checking",OTA_UI_CHECKING},
                               {"confirm",OTA_UI_CONFIRM},{"download",OTA_UI_DOWNLOAD},
                               {"verify",OTA_UI_VERIFY},{"ok",OTA_UI_OK},
                               {"uptodate",OTA_UI_UPTODATE},{"fail",OTA_UI_FAIL},{0,0}};
static const nv_t T_SCREEN[] = {{"thermostat",SCREEN_THERMOSTAT},{"fanvane",SCREEN_FANVANE},
                               {"menu",SCREEN_MENU},{"mode",SCREEN_MODE},
                               {"settings",SCREEN_SETTINGS},{"update",SCREEN_UPDATE},
                               {"about",SCREEN_ABOUT},{"wifi",SCREEN_WIFI},
                               {"zones",SCREEN_ZONES},{"advanced",SCREEN_ADVANCED},
                               {"room_source",SCREEN_ROOM_SOURCE},{0,0}};
/* Settings-ring / Advanced-row name tables — mirror the private SET_ / ADV_
 * enum ORDER by hand. For T_ADV_SEL the hand-mirroring is load-bearing, not
 * a workaround: adv_row_t is a typedef private to ui_advanced.c, never
 * declared in any header sim_cmd.c can include (see T_SCREEN's own
 * precedent above). ui_settings_sel()/ui_adv_sel() (ui.h) return the raw
 * int; keep this in step with ui_internal.h's SET_ enum / ui_advanced.c's
 * adv_row_t if either one's order ever changes. */
static const nv_t T_SET_SEL[] = {{"brightness",0},{"wifi",1},{"link",2},{"homekit",3},
                               {"heatpump",4},{"about",5},{"advanced",6},{"back",7},{0,0}};
static const nv_t T_ADV_SEL[] = {{"units",0},{"lock",1},{"idle",2},{"night",3},
                               {"motion",4},{"heatpump",5},{"reset",6},{"back",7},{0,0}};
static const nv_t T_GATE[]  = {{"none",UI_SCREEN_GATE_NONE},{"on",UI_SCREEN_GATE_ON},
                               {"off",UI_SCREEN_GATE_OFF},{0,0}};
static const nv_t T_BADGE[] = {{"offline",UI_ZB_OFFLINE},{"stale",UI_ZB_STALE},
                               {"online",UI_ZB_ONLINE},{"fault",UI_ZB_FAULT},
                               {"checking",UI_ZB_CHECKING},{0,0}};

/* Every font face in the build, by name. fontcheck reports the face a missing
 * glyph belongs to, and lv_font_t carries no name, so the mapping lives here.
 * This list IS the type scale (see ui_fonts_dial.h) — keep it in step with it.
 * Deleting a face from main/fonts/ while it is still named here fails to link,
 * which is deliberate: it makes the one written-down list impossible to forget. */
static const struct { const lv_font_t *f; const char *name; } FC_FACE[] = {
    { &jakarta_600_230, "jakarta_600_230" },   /* setpoint hero  — digits    */
    { &jakarta_600_128, "jakarta_600_128" },   /* band dual hero — digits    */
    { &jakarta_500_56,  "jakarta_500_56"  },   /* degree mark    — ° only    */
    { &jakarta_600_58,  "jakarta_600_58"  },
    { &jakarta_600_40,  "jakarta_600_40"  },
    { &jakarta_600_33,  "jakarta_600_33"  },   /* brightness     — digits+%  */
    { &jakarta_600_27,  "jakarta_600_27"  },
    { &jakarta_600_22,  "jakarta_600_22"  },
    { &jakarta_600_18,  "jakarta_600_18"  },
    { &jakarta_600_15,  "jakarta_600_15"  },
    { &jakarta_800_15,  "jakarta_800_15"  },   /* alert chip / About headers — weight 800 */
    { &jakarta_600_18_latin, "jakarta_600_18_latin" },
    { &jakarta_600_27_latin, "jakarta_600_27_latin" },
    { &jakarta_600_40_latin, "jakarta_600_40_latin" },
    { NULL, NULL }
};

static const char *fc_face_name(const lv_font_t *f)
{
    for (unsigned i = 0; FC_FACE[i].f; i++)
        if (FC_FACE[i].f == f) return FC_FACE[i].name;
    return "?";
}

static ui_zone_info_t s_simz[UI_MAX_ZONES];
static int s_simz_n = 0, s_simz_a = 0;

/* the dial would send an encrypted WIFI_SETUP here — count + print instead */
/* Simulator callbacks: requests are observed; tests choose when replies arrive. */
static uint32_t s_test_wifi_session;
void sim_wifi_session_start(uint32_t session)
{
    s_test_wifi_session = session;
    sim_cmd_on_wifi_setup();
}
void sim_wifi_cancel(uint32_t session)
{
    printf("CMD: wifi cancel %u\n", (unsigned)session);
}

static int s_wifi_setup_reqs = 0;
void sim_cmd_on_wifi_setup(void)
{
    s_wifi_setup_reqs++;
    printf("CMD: wifi setup\n");
}

static int nv_find(const nv_t *t, const char *s, int *out)
{
    for (; t->name; t++) if (!strcasecmp(t->name, s)) { *out = t->val; return 0; }
    char *end; long v = strtol(s, &end, 10);
    if (*s && !*end) { *out = (int)v; return 0; }
    return -1;
}

typedef enum { K_INT, K_BOOL, K_U8, K_I8, K_I16, K_U16, K_U32, K_ENUM, K_STR, K_CSTR } kind_t;
typedef struct { const char *name; kind_t k; size_t off; size_t size; const nv_t *tab; } field_t;
#define F(n,k,f)  {n,k,offsetof(hvac_state_t,f),0,NULL}
#define FE(n,f,t) {n,K_ENUM,offsetof(hvac_state_t,f),sizeof(((hvac_state_t*)0)->f),t}
#define FS(n,f)   {n,K_STR,offsetof(hvac_state_t,f),sizeof(((hvac_state_t*)0)->f),NULL}
static const field_t FIELDS[] = {
    FE("mode",mode,T_MODE), FE("status",status,T_STAT),
    F("room",K_INT,room_temp), F("room_temp",K_INT,room_temp),
    F("set",K_INT,set_dd),     F("set_dd",K_INT,set_dd),   /* deci-display */
    F("set_lo",K_INT,set_lo_dd), F("set_hi",K_INT,set_hi_dd), /* HEAT_COOL band */
    F("band_valid",K_BOOL,band_valid),
    F("use_f",K_BOOL,use_f),   F("humidity",K_INT,humidity),
    F("humidity_valid",K_BOOL,humidity_valid),
    FE("alert",alert,T_ALERT),
    F("fan",K_U8,fan_pct), F("fan_pct",K_U8,fan_pct),   /* 0=auto, 1..100 */
    FE("vvane",vvane,T_VV), FE("hvane",hvane,T_HV),
    F("comp_freq_hz",K_INT,comp_freq_hz),
    {"comp_stage",K_CSTR,offsetof(hvac_state_t,comp_stage),0,NULL},
    {"sub_mode",K_CSTR,offsetof(hvac_state_t,sub_mode),0,NULL},
    F("outside_temp",K_INT,outside_temp), F("outside_valid",K_BOOL,outside_valid),
    F("cn105_connected",K_BOOL,cn105_connected), F("poll_ms",K_INT,poll_ms),
    F("err_count",K_U32,err_count), F("error_code",K_U16,error_code),
    F("sensor_batt_low",K_BOOL,sensor_batt_low), F("sensor_batt_pct",K_INT,sensor_batt_pct),
    FS("hk_code",hk_code), F("hk_paired",K_INT,hk_paired), F("hk_connected",K_BOOL,hk_connected),
    F("wifi_connected",K_BOOL,wifi_connected), FS("wifi_ssid",wifi_ssid), FS("wifi_ip",wifi_ip),
    F("wifi_rssi",K_INT,wifi_rssi),
    F("wifi_provisioned",K_BOOL,wifi_provisioned), F("wifi_setup_avail",K_BOOL,wifi_setup_avail),
    F("wifi_setup_ap",K_BOOL,wifi_setup_ap),
    FS("hk_payload",hk_payload),
    F("diag_seen",K_BOOL,diag_seen), FS("unit_fw",unit_fw), FS("unit_build",unit_build),
    F("unit_uptime_s",K_U32,unit_uptime_s), F("unit_reset_reason",K_U8,unit_reset_reason),
    F("unit_wifi_ch",K_U8,unit_wifi_ch), F("auto_sub_mode",K_U8,auto_sub_mode),
    F("runtime_h",K_U32,runtime_h), F("runtime_valid",K_BOOL,runtime_valid),
    F("ble_valid",K_BOOL,ble_valid), F("ble_temp_dc",K_I16,ble_temp_dc),
    F("ble_hum_pct",K_U8,ble_hum_pct), F("ble_rssi",K_I8,ble_rssi),
    F("room_src",K_U8,room_src), F("room_src_status",K_U8,room_src_status),
    F("link_sens_present",K_BOOL,link_sens_present),
    F("link_sens_valid",K_BOOL,link_sens_valid),
    F("link_temp_dc",K_I16,link_temp_dc), F("link_hum_pct",K_U8,link_hum_pct),
    {NULL,0,0,0,NULL}
};

static int parse_bool(const char *v, bool *out)
{
    if (!strcasecmp(v,"1")||!strcasecmp(v,"true")||!strcasecmp(v,"on"))  { *out = true;  return 0; }
    if (!strcasecmp(v,"0")||!strcasecmp(v,"false")||!strcasecmp(v,"off")) { *out = false; return 0; }
    return -1;
}

static int set_field(hvac_state_t *s, const char *name, const char *val)
{
    for (const field_t *f = FIELDS; f->name; f++) {
        if (strcasecmp(f->name, name)) continue;
        void *p = (uint8_t *)s + f->off;
        switch (f->k) {
        case K_INT:  *(int *)p = atoi(val); return 0;
        case K_U8:   *(uint8_t *)p  = (uint8_t)atoi(val);  return 0;
        case K_I8:   *(int8_t *)p   = (int8_t)atoi(val);   return 0;
        case K_I16:  *(int16_t *)p  = (int16_t)atoi(val);  return 0;
        /* base 0, not atoi: fault codes are written in hex ("error_code=0x62"), and
         * every other numeric kind here is base-10 — atoi would silently yield 0. */
        case K_U16:  *(uint16_t *)p = (uint16_t)strtoul(val, NULL, 0); return 0;
        case K_U32:  *(uint32_t *)p = (uint32_t)strtoul(val, NULL, 10); return 0;
        case K_BOOL: { bool b; if (parse_bool(val, &b)) return -1; *(bool *)p = b; return 0; }
        case K_ENUM: { int e; if (nv_find(f->tab, val, &e)) return -1;
                       memcpy(p, &e, f->size); return 0; }
        case K_STR:  snprintf((char *)p, f->size, "%s", val); return 0;
        case K_CSTR: /* "none" -> "" (NORMAL); else leaked strdup — fine for a sim */
                     *(const char **)p = strcasecmp(val, "none") ? strdup(val) : "";
                     return 0;
        }
    }
    return -1;
}

static int err(const char *msg) { fprintf(stderr, "sim_cmd: %s\n", msg); return -1; }

/* --- hitmap: true hit box of every visible clickable on the active screen ---
 * Probes with lv_obj_hit_test() (public, and it already accounts for
 * ext_click_area + radius shaping) rather than re-deriving geometry, then
 * intersects the result with the ancestor clip: lv_indev_search_obj() only
 * descends into a child when the point lies inside the PARENT's coords, so
 * every ancestor box clips its descendants' hit boxes — a row scrolled out of
 * a scrollable container, or an ext_click_area that spills past a bounded
 * parent, is NOT touchable even though lv_obj_hit_test() says it is.
 *
 * CAVEAT: hitmap models HIDDEN, ancestor clipping/scrolling, ext_click_area
 * and radius shaping (ADV_HITTEST). It does NOT model z-order overlap (a
 * topmost sibling can silently own part of a lower one's box), transforms, or
 * mid-animation geometry. A clickable clipped to nothing reports w=h=min=0. */
static bool hm_hit(lv_obj_t *o, const lv_area_t *clip, lv_point_t p)
{
    return lv_area_is_point_on(clip, &p, 0) && lv_obj_hit_test(o, &p);
}

static int hm_grow(lv_obj_t *o, const lv_area_t *clip, lv_point_t c, int dx, int dy)
{
    int n = 0;
    for (;;) {
        lv_point_t p = { c.x + dx * (n + 1), c.y + dy * (n + 1) };
        if (!hm_hit(o, clip, p)) break;
        if (++n > 400) break;                     /* full-screen panels */
    }
    return n;
}

/* first descendant label's text — makes a hit line identifiable by a human */
static const char *hm_label(lv_obj_t *o)
{
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && *t) return t;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        const char *t = hm_label(lv_obj_get_child(o, i));
        if (t) return t;
    }
    return NULL;
}

/* depth-first search for the object whose OWN text is an exact match — unlike
 * hm_walk (which only reports CLICKABLE objects), this finds any label, so a
 * plain non-clickable child's drawn box can be read back live. Exact match,
 * first hit wins. */
/* Every VISIBLE label's text, one line each. The copy tests assert on WORDING
 * ("no P6 survives anywhere", "the dialog quotes the code"), which needs
 * substring matching over multi-word text — labelbox only exact-matches a single
 * whitespace-delimited token, so nothing could see a fabricated code buried in a
 * longer string. Newlines are escaped so a two-line label ("CN105\nOFFLINE")
 * stays one grep-able line. */
/* tree — every VISIBLE object: its class and its drawn box (and, for labels, its text).
 *
 * The STRUCTURAL fingerprint of a face: what is on screen, and where. It is the right
 * instrument for a TRANSITION test, and the other two are not:
 *
 *   - pixels cry wolf on animation PHASE. The alert arc's sweep carries over between
 *     two offline faces (same ring, never restarted), so the same face reached two
 *     ways diffs on the sweep alone while being visually identical.
 *   - `labels` is text-only, so it is blind to a leaked ICON or DOT — which is exactly
 *     what leaks (T.pairIcon, T.offIcon, T.setDot).
 *
 * This sees every widget and ignores phase: a sweeping arc changes its ANGLES, not its
 * box; a breathing ring changes its OPACITY, not its geometry.
 *
 * A hidden parent returns early, which is correct — LVGL does not draw its children. */
static void tree_walk(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const char *cls = "obj";
    if      (lv_obj_check_type(o, &lv_label_class)) cls = "label";
    else if (lv_obj_check_type(o, &lv_image_class)) cls = "image";
    else if (lv_obj_check_type(o, &lv_arc_class))   cls = "arc";
    printf("vis %-5s %d,%d,%d,%d", cls, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        fputs(" \"", stdout);
        for (const char *p = t ? t : ""; *p; p++) {
            if (*p == '\n') fputs("\\n", stdout);
            else             fputc(*p, stdout);
        }
        fputc('"', stdout);
    }
    fputc('\n', stdout);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        tree_walk(lv_obj_get_child(o, i));
}

static void lbl_walk(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && *t) {
            fputs("label text=", stdout);
            for (const char *p = t; *p; p++) {
                if (*p == '\n') fputs("\\n", stdout);
                else            fputc(*p, stdout);
            }
            fputc('\n', stdout);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        lbl_walk(lv_obj_get_child(o, i));
}

/* fontcheck — every visible label's text, character by character, asked of the
 * font it will actually be drawn with. LVGL draws a missing glyph as a
 * placeholder box ("tofu"): the text is right, the pixels are wrong. That is
 * silent in a PNG diff and invisible to `labels`, and the dial's faces are
 * SUBSET-generated (fonts/README_fonts.md), with bounded Latin coverage.
 * So any copy edit reaching for an em dash, an ellipsis or an
 * arrow, and any change repointing a FONT_* macro at a face with a narrower
 * range, is one keystroke away from tofu. This names the exact (face, codepoint)
 * pairs that would render as boxes. Zero missing = clean. */
static unsigned fc_missing, fc_checked;

static void fc_walk(lv_obj_t *o)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        const lv_font_t *f = lv_obj_get_style_text_font(o, LV_PART_MAIN);
        if (t && *t && f) {
            uint32_t i = 0;
            while (t[i]) {
                uint32_t cp = lv_text_encoded_next(t, &i);
                if (!cp || cp == '\n') continue;
                fc_checked++;
                lv_font_glyph_dsc_t g;
                if (!lv_font_get_glyph_dsc(f, &g, cp, 0)) {
                    fc_missing++;
                    printf("fontcheck MISSING face=%s cp=U+%04X text=%s\n",
                           fc_face_name(f), (unsigned)cp, t);
                } else {
                    lv_font_glyph_release_draw_data(&g);
                }
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        fc_walk(lv_obj_get_child(o, i));
}

/* `pill` = report the label's PARENT (the mkpill button) instead of the label.
 * A geometry test on a BUTTON wants the button's drawn box, and reconstructing it
 * from the label's box means the test has to know the pill's padding — which is
 * not one number: the pairing face re-cuts its pills tighter when two share the
 * row. Ask LVGL for the box that was actually drawn. */
static bool lb_find2(lv_obj_t *o, const char *text, lv_area_t *out, bool pill)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
    if (lv_obj_check_type(o, &lv_label_class)) {
        const char *t = lv_label_get_text(o);
        if (t && !strcmp(t, text)) {
            lv_obj_t *tgt = o;
            if (pill) {
                lv_obj_t *p = lv_obj_get_parent(o);
                if (p) tgt = p;
            }
            lv_obj_get_coords(tgt, out);
            return true;
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        if (lb_find2(lv_obj_get_child(o, i), text, out, pill)) return true;
    return false;
}
static bool lb_find(lv_obj_t *o, const char *text, lv_area_t *out)
{
    return lb_find2(o, text, out, false);
}

static void hm_walk(lv_obj_t *o, const lv_area_t *clip)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;   /* hit_test ignores HIDDEN */

    lv_area_t a;
    lv_obj_get_coords(o, &a);

    if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
        /* seed the probe inside the *reachable* part of the object */
        lv_area_t vis;
        bool on = lv_area_intersect(&vis, &a, clip);
        lv_point_t c = { (a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2 };
        int w = 0, h = 0;
        if (on) {
            c.x = (vis.x1 + vis.x2) / 2;
            c.y = (vis.y1 + vis.y2) / 2;
            if (hm_hit(o, clip, c)) {
                w = hm_grow(o, clip, c, -1, 0) + hm_grow(o, clip, c, 1, 0) + 1;
                h = hm_grow(o, clip, c, 0, -1) + hm_grow(o, clip, c, 0, 1) + 1;
            }
        }
        const char *t = hm_label(o);
        /* box= is the DRAWN rect (no ext_click_area): the adjacency gate needs
         * the real edges to work out where the gap between two pills is, and
         * hence where their hit boxes are supposed to meet. Keep `text=` last —
         * a label may contain spaces. */
        printf("hit x=%d y=%d w=%d h=%d min=%d drawn=%dx%d box=%d,%d,%d,%d text=%s\n",
               c.x, c.y, w, h, w < h ? w : h,
               (int)lv_area_get_width(&a), (int)lv_area_get_height(&a),
               (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
               t ? t : "-");
    }

    /* children are hit only through this object's box (lv_indev_search_obj) */
    lv_area_t sub = a;
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
        int32_t ext = lv_obj_get_ext_draw_size(o);
        lv_area_increase(&sub, ext, ext);
    }
    lv_area_t cclip;
    if (!lv_area_intersect(&cclip, &sub, clip)) cclip = (lv_area_t){ 0, 0, -1, -1 };
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        hm_walk(lv_obj_get_child(o, i), &cclip);
}

int sim_cmd_exec(const char *line)
{
    char buf[512];
    snprintf(buf, sizeof buf, "%s", line);
    char *save = NULL;
    char *cmd = strtok_r(buf, " \t\r\n", &save);
    if (!cmd || cmd[0] == '#') return 0;

    if (!strcmp(cmd, "state")) {
        hvac_state_t s; ui_get_state(&s);
        char *kv; int n = 0;
        while ((kv = strtok_r(NULL, " \t\r\n", &save))) {
            char *eq = strchr(kv, '=');
            if (!eq) return err("state: expected field=value");
            *eq = 0;
            if (set_field(&s, kv, eq + 1)) {
                fprintf(stderr, "sim_cmd: state: bad field/value '%s=%s'\n", kv, eq + 1);
                return -1;
            }
            n++;
        }
        if (!n) return err("state: no assignments");
        ui_set_state(&s);
        return 0;
    }
    if (!strcmp(cmd, "screen")) {
        char *a = strtok_r(NULL, " \t\r\n", &save); int id;
        if (!a || nv_find(T_SCREEN, a, &id)) return err("screen: unknown name");
        ui_show_screen((screen_id_t)id);
        return 0;
    }
    if (!strcmp(cmd, "bootface")) {   /* build the boot-reset countdown face at <pct> fill */
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a) return err("bootface: <pct 0..100>");
        factory_reset_face_build(atoi(a));
        return 0;
    }
    if (!strcmp(cmd, "caps")) {  /* caps [modes=0x..] [features=0x..] [fan_steps=N]
                                  * [fan_auto=0|1] [vane_v=0xNN] [vane_h=0xNN]
                                  * [min_dc=N] [max_dc=N] [step_dc=N] [ftab=N]
                                  * partial keys mutate a persistent full-default
                                  * snapshot, then push ui_set_caps() */
        static ui_caps_t c = {
            .modes = 0x0077 /* native: no HEAT_COOL bit */, .features = 0x00FF, .fan_steps = 5,
            .fan_has_auto = true, .vane_v = 0x35 /* CAP(5,auto,swing) */,
            .vane_h = 0x26 /* CAP(6,-,swing) */, .set_min_dc = 160,
            .set_max_dc = 305, .set_step_dc = 5, .ftab_id = 1,
            .band_min_gap_dc = 5,
        };
        char *kv;
        while ((kv = strtok_r(NULL, " \t\r\n", &save))) {
            char *eq = strchr(kv, '=');
            if (!eq) return err("caps: expected key=value");
            *eq = 0;
            long v = strtol(eq + 1, NULL, 0);       /* accepts 0x.. and decimal */
            if      (!strcmp(kv, "modes"))     c.modes = (uint16_t)v;
            else if (!strcmp(kv, "features"))  c.features = (uint16_t)v;
            else if (!strcmp(kv, "fan_steps")) c.fan_steps = (uint8_t)v;
            else if (!strcmp(kv, "fan_auto"))  c.fan_has_auto = v != 0;
            else if (!strcmp(kv, "vane_v"))    c.vane_v = (uint8_t)v;
            else if (!strcmp(kv, "vane_h"))    c.vane_h = (uint8_t)v;
            else if (!strcmp(kv, "min_dc"))    c.set_min_dc = (int16_t)v;
            else if (!strcmp(kv, "max_dc"))    c.set_max_dc = (int16_t)v;
            else if (!strcmp(kv, "step_dc"))   c.set_step_dc = (uint8_t)v;
            else if (!strcmp(kv, "ftab"))      c.ftab_id = (uint8_t)v;
            else if (!strcmp(kv, "band_gap"))  c.band_min_gap_dc = (uint8_t)v;
            else { fprintf(stderr, "sim_cmd: caps: unknown key '%s'\n", kv); return -1; }
        }
        ui_set_caps(&c);
        return 0;
    }
    if (!strcmp(cmd, "turn")) {
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a) return err("turn: need count");
        int n = atoi(a), step = n >= 0 ? 1 : -1;
        for (int i = 0; i < abs(n); i++) { ui_encoder(step); sim_time_wait(30); }
        return 0;
    }
    if (!strcmp(cmd, "press")) { ui_button_down(); sim_time_wait(30); ui_button_up(); ui_button(); return 0; }
    if (!strcmp(cmd, "tap")) {           /* tap <x> <y> — synthetic touch (headless) */
        char *x = strtok_r(NULL, " \t\r\n", &save);
        char *y = strtok_r(NULL, " \t\r\n", &save);
        if (!x || !y) return err("tap: <x> <y>");
        sim_tap(atoi(x), atoi(y));
        sim_time_wait(120);              /* let LVGL poll press + release */
        return 0;
    }
    if (!strcmp(cmd, "swipe")) {         /* swipe <x0> <y0> <x1> <y1> <ms> — timed drag path */
        char *x0 = strtok_r(NULL, " \t\r\n", &save);
        char *y0 = strtok_r(NULL, " \t\r\n", &save);
        char *x1 = strtok_r(NULL, " \t\r\n", &save);
        char *y1 = strtok_r(NULL, " \t\r\n", &save);
        char *ms = strtok_r(NULL, " \t\r\n", &save);
        if (!x0 || !y0 || !x1 || !y1 || !ms) return err("swipe: <x0> <y0> <x1> <y1> <ms>");
        int ms_i = atoi(ms);
        if (ms_i < 1) ms_i = 1;                    /* mirror sim_swipe's floor: no wrap, no hang */
        sim_swipe(atoi(x0), atoi(y0), atoi(x1), atoi(y1), ms_i);
        sim_time_wait((uint32_t)ms_i + 150);       /* play the path + the release poll */
        return 0;
    }
    if (!strcmp(cmd, "touchhold")) {     /* touchhold <x> <y> — press and HOLD (non-blocking) */
        char *x = strtok_r(NULL, " \t\r\n", &save);
        char *y = strtok_r(NULL, " \t\r\n", &save);
        if (!x || !y) return err("touchhold: <x> <y>");
        sim_touch_hold(atoi(x), atoi(y));
        sim_time_wait(60);               /* let LVGL observe the press edge */
        return 0;
    }
    if (!strcmp(cmd, "touchup")) {       /* release a touchhold */
        sim_touch_up();
        sim_time_wait(120);              /* let LVGL observe the release + synthesize CLICKED */
        return 0;
    }
    if (!strcmp(cmd, "hitmap")) {        /* true hit box of every visible clickable */
        printf("hitmap floor=%d disp=%d\n", (int)SERIN_TOUCH_MIN, SERIN_DISP);
        /* the alert dialog is built on lv_layer_top(), not on the screen, so
         * walk both roots or its two buttons are invisible to the gate */
        lv_obj_t *root[2] = { lv_screen_active(), lv_layer_top() };
        for (unsigned r = 0; r < 2; r++) {
            lv_area_t clip;
            lv_obj_get_coords(root[r], &clip);
            hm_walk(root[r], &clip);
        }
        fflush(stdout);
        return 0;
    }
    /* hitat <x> <y> — which object would actually RECEIVE a click at (x,y)?
     * Calls LVGL's own lv_indev_search_obj() on the same roots, in the same
     * order, that indev_proc_press() does, so the answer includes the two
     * things `hitmap` cannot model: z-order between overlapping siblings, and
     * the parent-box gate. This is what proves a boundary between two adjacent
     * buttons is clean — each side of the midpoint must name its own button,
     * with no pixel naming the other one and none naming the background. */
    if (!strcmp(cmd, "hitat")) {
        char *xs = strtok_r(NULL, " \t\r\n", &save);
        char *ys = strtok_r(NULL, " \t\r\n", &save);
        if (!xs || !ys) return err("hitat: expected <x> <y>");
        lv_point_t p = { atoi(xs), atoi(ys) };
        lv_obj_t *root[2] = { lv_layer_top(), lv_screen_active() };   /* top layer first, as indev does */
        lv_obj_t *found = NULL;
        for (unsigned r = 0; r < 2 && !found; r++) found = lv_indev_search_obj(root[r], &p);
        const char *t = found ? hm_label(found) : NULL;
        printf("hitat x=%d y=%d obj=%s text=%s\n", (int)p.x, (int)p.y,
               found ? "yes" : "none", t ? t : "-");
        fflush(stdout);
        return 0;
    }
    /* labelbox <text> — drawn box of the label whose own text exactly matches,
     * wherever it sits and whether or not it is clickable. hitmap only reports
     * CLICKABLE objects, so a hit-test split keyed off a plain child's edges
     * (e.g. two labels sharing one clickable ancestor) has no other way to be
     * checked against live geometry instead of hard-coded pixels. */
    if (!strcmp(cmd, "labelbox")) {
        char *t = strtok_r(NULL, " \t\r\n", &save);
        if (!t) return err("labelbox: expected <text>");
        lv_area_t a; bool found = false;
        lv_obj_t *root[2] = { lv_screen_active(), lv_layer_top() };
        for (unsigned r = 0; r < 2 && !found; r++) found = lb_find(root[r], t, &a);
        if (found)
            printf("labelbox text=%s box=%d,%d,%d,%d\n", t,
                   (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
        else
            printf("labelbox text=%s obj=none\n", t);
        fflush(stdout);
        return 0;
    }
    /* pillbox <text> — the drawn box of the BUTTON carrying that label (the label's
     * parent). What a geometry assertion on a pill actually wants; see lb_find2. */
    if (!strcmp(cmd, "pillbox")) {
        char *t = strtok_r(NULL, " \t\r\n", &save);
        if (!t) return err("pillbox: expected <text>");
        lv_area_t a; bool found = false;
        lv_obj_t *root[2] = { lv_screen_active(), lv_layer_top() };
        for (unsigned r = 0; r < 2 && !found; r++) found = lb_find2(root[r], t, &a, true);
        if (found)
            printf("pillbox text=%s box=%d,%d,%d,%d\n", t,
                   (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
        else
            printf("pillbox text=%s obj=none\n", t);
        fflush(stdout);
        return 0;
    }
    if (!strcmp(cmd, "tree")) {   /* every visible object: class + box (+ text). See tree_walk. */
        lv_obj_t *root[2] = { lv_screen_active(), lv_layer_top() };
        for (unsigned r = 0; r < 2; r++) tree_walk(root[r]);
        fflush(stdout);
        return 0;
    }
    if (!strcmp(cmd, "labels")) {   /* every visible label's text — the copy assertion */
        lv_obj_t *root[2] = { lv_screen_active(), lv_layer_top() };
        for (unsigned r = 0; r < 2; r++) lbl_walk(root[r]);
        fflush(stdout);
        return 0;
    }
    if (!strcmp(cmd, "fontcheck")) {   /* the TOFU assertion — see fc_walk */
        fc_missing = fc_checked = 0;
        lv_obj_t *root[2] = { lv_screen_active(), lv_layer_top() };
        for (unsigned r = 0; r < 2; r++) fc_walk(root[r]);
        printf("fontcheck missing=%u checked=%u\n", fc_missing, fc_checked);
        fflush(stdout);
        return fc_missing ? -1 : 0;
    }
    if (!strcmp(cmd, "info")) {          /* one-line UI state readback */
        hvac_state_t s; ui_get_state(&s);
        const char *scr = "?", *md = "?", *setn = "?", *advn = "?";
        for (const nv_t *t = T_SCREEN; t->name; t++)
            if (t->val == (int)ui_current_screen()) { scr = t->name; break; }
        for (const nv_t *t = T_MODE; t->name; t++)
            if (t->val == (int)s.mode) { md = t->name; break; }
        for (const nv_t *t = T_SET_SEL; t->name; t++)
            if (t->val == ui_settings_sel()) { setn = t->name; break; }
        for (const nv_t *t = T_ADV_SEL; t->name; t++)
            if (t->val == ui_adv_sel()) { advn = t->name; break; }
        printf("info screen=%s face=%s mode=%s set=%d set_lo=%d set_hi=%d edge=%s fan=%d zone=%d/%d glance=%d disp_sleep=%d night=%d anim=%d wz=%s wz_req=%d hold=%d adv_sel=%s units=%c set_sel=%s lock=%d lock_win=%d dlg=%s reset_fired=%d coach=%d\n",
               scr, ui_dial_face(), md, s.set_dd, s.set_lo_dd, s.set_hi_dd,
               ui_band_edge() ? "hi" : "lo", (int)s.fan_pct,
               ui_active_zone(), ui_zone_count(), ui_glancing(), sim_disp_asleep(),
               ui_night_active(), ui_anim_en_get(),
               ui_wifi_face(), s_wifi_setup_reqs, ui_hold_arc_shown(),
               advn, ui_units_c() ? 'C' : 'F', setn,
               ui_child_lock_enabled(), ui_child_lock_window_open(),
               ui_dlg_state(), sim_reset_fired() ? 1 : 0, ui_coach_active());
        fflush(stdout);
        return 0;
    }
    if (!strcmp(cmd, "long"))  { ui_button_down(); sim_time_wait(750); ui_button_long(); ui_button_up(); return 0; }
    if (!strcmp(cmd, "down"))  { sim_press_down(); return 0; }   /* arms the 700ms hold-fires-long path */
    if (!strcmp(cmd, "up"))    { sim_press_up();   return 0; }
    if (!strcmp(cmd, "pair")) {
        char *a = strtok_r(NULL, " \t\r\n", &save); int st;
        if (!a || nv_find(T_PAIR, a, &st)) return err("pair: hidden|prompt|searching|ok|fail|full|mismatch [ch]");
        char *c = strtok_r(NULL, " \t\r\n", &save);
        ui_set_pairing((pair_ui_t)st, c ? (uint8_t)atoi(c) : 0);
        return 0;
    }
    if (!strcmp(cmd, "gate")) {   /* remote screen gate: none|on|off (ui.h) */
        char *a = strtok_r(NULL, " \t\r\n", &save); int g;
        if (!a || nv_find(T_GATE, a, &g)) return err("gate: none|on|off");
        ui_set_screen_gate((ui_screen_gate_t)g);
        return 0;
    }
    if (!strcmp(cmd, "night")) {   /* sun gate: on|off [ceil%] (ui.h ui_set_night) */
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a || (strcmp(a, "on") && strcmp(a, "off")))
            return err("night: on|off [ceil]");
        char *c = strtok_r(NULL, " \t\r\n", &save);
        ui_set_night(!strcmp(a, "on"), c ? (uint8_t)atoi(c) : 0);
        return 0;
    }
    if (!strcmp(cmd, "bright")) {  /* test hook: the Settings track's edit path */
        char *a = strtok_r(NULL, " \t\r\n", &save);
        int p = a ? atoi(a) : -1;
        if (p < 10 || p > 100) return err("bright: 10..100");
        s_brightness = p;
        apply_brightness();
        return 0;
    }
    if (!strcmp(cmd, "coach")) {   /* coach arm — re-arm the first-run coach */
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a || strcmp(a, "arm")) return err("coach: expected 'arm'");
        ui_coach_arm();
        return 0;
    }
    if (!strcmp(cmd, "ota")) {
        char *a = strtok_r(NULL, " \t\r\n", &save); int st;
        if (!a || nv_find(T_OTA, a, &st)) return err("ota: <state> [pct] [txt]");
        char *p = strtok_r(NULL, " \t\r\n", &save);
        char *t = strtok_r(NULL, "\r\n", &save);          /* rest of line */
        ui_set_ota((ota_ui_t)st, p ? atoi(p) : 0, t);
        return 0;
    }
    if (!strcmp(cmd, "bridge")) {
        char *b = strtok_r(NULL, " \t\r\n", &save);
        char *o = strtok_r(NULL, " \t\r\n", &save);
        char *r = strtok_r(NULL, " \t\r\n", &save);
        bool bb, oo;
        if (!b || !o || !r || parse_bool(b, &bb) || parse_bool(o, &oo))
            return err("bridge: <bonded 0|1> <online 0|1> <rssi>");
        static const uint8_t MAC[6] = { 0xB0, 0x81, 0x84, 0x95, 0x42, 0x08 };
        ui_set_link_state(bb, oo, MAC, atoi(r));
        return 0;
    }
    if (!strcmp(cmd, "link")) {
        char *p = strtok_r(NULL, " \t\r\n", &save);
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!p || !a) return err("link: <peer_ver> <age_s>");
        sim_link_set(atoi(p), atoi(a));
        return 0;
    }
    /* fw <version> <build> — pin the strings the About page renders.
     *
     * sim_main seeds the build from __DATE__, which is a COMPILE-time constant:
     * the About page's pixels therefore change whenever the sim is rebuilt on a
     * new day. That is invisible until you keep a golden corpus, at which point
     * About goes red every midnight for no reason at all. The corpus needs a
     * STABLE string far more than it needs a truthful one. */
    if (!strcmp(cmd, "fw")) {
        char *v = strtok_r(NULL, " \t\r\n", &save);
        char *b = strtok_r(NULL, " \t\r\n", &save);
        if (!v || !b) return err("fw: <version> <build>");
        ui_set_fw_version(v);
        ui_set_fw_build(b);
        return 0;
    }
    if (!strcmp(cmd, "wait")) {
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a) return err("wait: need ms");
        sim_time_wait((uint32_t)atoi(a));
        return 0;
    }
    if (!strcmp(cmd, "shot")) {
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a) return err("shot: need path");
        return sim_shot(a);
    }
    if (!strcmp(cmd, "shotfb")) {   /* the retained panel, stale pixels and all */
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a) return err("shotfb: need path");
        return sim_shot_fb(a);
    }
    /* Force a FULL repaint of the screen without changing a single piece of state.
     * The ground truth for partial-invalidation bugs: shot / redraw / shot must be
     * pixel-identical, because the only thing that differs is how much of the frame
     * LVGL was told to repaint. Anything an animation forgot to invalidate — a
     * stale sweep segment, a rim that moved without dirtying where it used to be —
     * survives in the incremental frame and vanishes in this one. */
    if (!strcmp(cmd, "redraw")) {
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
        /* lv_refr_now(), NOT a sim_time_wait(): the whole point is to repaint the
         * SAME frame. Waiting would advance the virtual clock, step every running
         * animation, and compare two different moments — which reads as "the rim
         * left something behind" on faces whose invalidation is perfectly fine. */
        lv_refr_now(NULL);
        return 0;
    }
    /* Render whatever is ALREADY marked dirty, without advancing the clock. An
     * animation steps its value and invalidates on one lv_timer_handler pass, but the
     * display refresh can still be pending when the next command runs — so a raw
     * `shot` can be one frame behind the model. That staleness is not a bug in the
     * UI, and without this it would masquerade as one in `redraw`'s pixel diff.
     * Crucially this CANNOT paper over a real missed invalidation: an area nobody
     * marked dirty is not repainted here either. */
    if (!strcmp(cmd, "flush")) { lv_refr_now(NULL); return 0; }
    /* Pixels LVGL was asked to repaint since the last `dirty`, then reset. Put one
     * before the window you care about and one after; what falls between is the
     * render cost of whatever ran there, in the only unit the sim can honestly
     * report (a count, not a duration). */
    if (!strcmp(cmd, "dirty")) {
        uint32_t fl = 0;
        uint64_t n = sim_dirty_px_take(&fl);
        printf("dirty px=%llu flushes=%u screens=%.2f\n", (unsigned long long)n, fl,
               (double)n / (double)(SERIN_DISP * SERIN_DISP));
        return 0;
    }
    if (!strcmp(cmd, "quit")) { sim_quit = true; return 0; }
    if (!strcmp(cmd, "zones")) {          /* zones <n> [active] — n fake zones */
        char *a = strtok_r(NULL, " \t\r\n", &save);
        if (!a) return err("zones: <n> [active]");
        int n = atoi(a);
        if (n < 0 || n > UI_MAX_ZONES) return err("zones: n 0..7");
        char *act = strtok_r(NULL, " \t\r\n", &save);
        s_simz_n = n; s_simz_a = act ? atoi(act) : 0;
        for (int i = 0; i < n; i++) {
            memset(&s_simz[i], 0, sizeof s_simz[i]);
            snprintf(s_simz[i].name, sizeof s_simz[i].name, "Zone %d", i + 1);
            s_simz[i].mac[4] = 0xA0; s_simz[i].mac[5] = (uint8_t)i;
            s_simz[i].have_state = true;
            s_simz[i].room_temp = 68 + i; s_simz[i].use_f = true;
            s_simz[i].mode = MODE_HEAT; s_simz[i].badge = UI_ZB_ONLINE;
        }
        ui_set_zones(s_simz, s_simz_n, s_simz_a);
        return 0;
    }
    /* roomsrc <sel> <name>[,<name>...] [status] — stand in for the controller's
     * SL2_PKT_ROOM_CATALOG_RESP so SCREEN_ROOM_SOURCE is reachable headless.
     * Ids are synthesised 1..n and <sel> is a 0-based index into the list, so a
     * capture names the row it wants without knowing the wire id scheme. */
if (!strcmp(cmd, "wifi_ack")) {
    char *status = strtok_r(NULL, " \t\r\n", &save);
    char *session = strtok_r(NULL, " \t\r\n", &save);
    if (!status) return err("wifi_ack: <status> [session]");
    ui_wifi_cancel_result(session ? (uint32_t)strtoul(session, NULL, 0) : s_test_wifi_session,
                          (uint8_t)atoi(status));
    return 0;
}

/* Test-only simulator command: stable IDs, optional kinds, and empty catalogs. */
if (!strcmp(cmd, "roomsrc_ids")) {
    char *selected = strtok_r(NULL, " \t\r\n", &save);
    char *list = strtok_r(NULL, " \t\r\n", &save);
    if (!selected || !list) return err("roomsrc_ids: <selected_id> <id[:kind]:name,...|->");
    struct sl2_room_source_entry entries[SL2_ROOM_CATALOG_MAX] = {0};
    int count = 0;
    char *item_save = NULL;
    if (strcmp(list, "-")) {
        for (char *item = strtok_r(list, ",", &item_save); item;
             item = strtok_r(NULL, ",", &item_save)) {
            char *colon = strchr(item, ':');
            if (!colon || count == SL2_ROOM_CATALOG_MAX) return err("roomsrc_ids: bad entry");
            *colon = '\0';
            entries[count].id = strtoull(item, NULL, 0);
            entries[count].flags = SL2_ROOM_SOURCE_F_SELECTABLE;
            char *name = colon + 1;
            char *kind_end = strchr(name, ':');
            if (kind_end) {
                *kind_end = '\0';
                entries[count].kind = (uint8_t)strtoul(name, NULL, 0);
                name = kind_end + 1;
            }
            snprintf(entries[count].name, sizeof entries[count].name, "%s", name);
            count++;
        }
    }
    ui_set_room_sources(entries, count, strtoull(selected, NULL, 0), SL2_ROOMST_OK);
    return 0;
}

    if (!strcmp(cmd, "roomsrc")) {
        char *selp = strtok_r(NULL, " \t\r\n", &save);
        char *list = strtok_r(NULL, " \t\r\n", &save);
        if (!selp || !list) return err("roomsrc: <sel> <name>[,<name>...] [status]");
        char *stp = strtok_r(NULL, " \t\r\n", &save);
        struct sl2_room_source_entry e[SL2_ROOM_CATALOG_MAX];
        int n = 0;
        for (char *t = strtok(list, ","); t && n < SL2_ROOM_CATALOG_MAX;
             t = strtok(NULL, ",")) {
            memset(&e[n], 0, sizeof e[n]);
            e[n].id = (uint64_t)(n + 1);
            e[n].flags = SL2_ROOM_SOURCE_F_SELECTABLE;
            snprintf(e[n].name, sizeof e[n].name, "%s", t);
            n++;
        }
        if (!n) return err("roomsrc: empty list");
        int sel = atoi(selp);
        if (sel < 0 || sel >= n) return err("roomsrc: sel out of range");
        ui_set_room_sources(e, n, (uint64_t)(sel + 1),
                            (uint8_t)(stp ? atoi(stp) : 0));
        ui_set_room_src_name(e[sel].name);
        return 0;
    }
    if (!strcmp(cmd, "zone")) {  /* zone <i> name=/badge=/temp=/mode=/status=/set=/lo=/hi=/state= */
        char *ix = strtok_r(NULL, " \t\r\n", &save);
        if (!ix) return err("zone: <i> k=v ...");
        int i = atoi(ix);
        if (i < 0 || i >= s_simz_n) return err("zone: bad index");
        char *kv;
        while ((kv = strtok_r(NULL, " \t\r\n", &save))) {
            char *eq = strchr(kv, '=');
            if (!eq) return err("zone: k=v expected");
            *eq = 0; const char *v = eq + 1; int ev;
            if (!strcmp(kv, "name"))
                snprintf(s_simz[i].name, sizeof s_simz[i].name, "%s", v);
            else if (!strcmp(kv, "badge") && !nv_find(T_BADGE, v, &ev))
                s_simz[i].badge = (ui_zone_badge_t)ev;
            else if (!strcmp(kv, "temp")) s_simz[i].room_temp = atoi(v);
            else if (!strcmp(kv, "mode") && !nv_find(T_MODE, v, &ev))
                s_simz[i].mode = (hvac_mode_t)ev;
            else if (!strcmp(kv, "status") && !nv_find(T_STAT, v, &ev))
                s_simz[i].status = (hvac_status_t)ev;
            else if (!strcmp(kv, "set"))  s_simz[i].set_dd = (int)(atof(v) * 10.0 + 0.5);
            else if (!strcmp(kv, "lo"))   s_simz[i].set_lo_dd = (int)(atof(v) * 10.0 + 0.5);
            else if (!strcmp(kv, "hi")) { s_simz[i].set_hi_dd = (int)(atof(v) * 10.0 + 0.5);
                                          s_simz[i].band_valid = true; }
            else if (!strcmp(kv, "state")) s_simz[i].have_state = atoi(v) != 0;
            else return err("zone: unknown key");
        }
        ui_set_zones(s_simz, s_simz_n, s_simz_a);
        return 0;
    }
    fprintf(stderr, "sim_cmd: unknown command '%s'\n", cmd);
    return -1;
}

int sim_cmd_run_string(const char *cmds)
{
    char *dup = strdup(cmds), *save = NULL;
    for (char *c = strtok_r(dup, ";", &save); c; c = strtok_r(NULL, ";", &save)) {
        if (sim_cmd_exec(c)) { free(dup); return -1; }
        if (sim_quit) break;
    }
    free(dup);
    return 0;
}

int sim_cmd_run_script(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "sim_cmd: cannot open %s\n", path); return -1; }
    char line[512]; int rc = 0;
    while (!rc && !sim_quit && fgets(line, sizeof line, f)) rc = sim_cmd_exec(line);
    fclose(f);
    return rc;
}
