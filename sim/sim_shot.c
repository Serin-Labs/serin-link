// SPDX-License-Identifier: GPL-3.0-only
/* sim_shot.c — composite the active screen and lv_layer_top (dialogs, the
 * alert explainer, idle overlays) into an RGB PNG. Works in both modes. */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "sim_shot.h"
#include "sim.h"
#include "serin_tokens.h"   /* SERIN_DISP */
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>

/* Dump the RETAINED panel buffer — the pixels a real display would be showing.
 *
 * sim_shot() below is an lv_snapshot, which re-renders the object tree into a fresh
 * buffer and so is FULL by construction: it cannot show a stale pixel, which means
 * it cannot see a missed invalidation either. Every screenshot test in the suite is
 * blind to that whole class of bug. This one isn't: RENDER_MODE_DIRECT repaints only
 * the invalidated areas of this buffer and leaves the rest alone, so anything an
 * animation forgot to dirty is still sitting here.
 *
 * rgb565, so do NOT diff this against sim_shot()'s 8-bit output — the quantisation
 * alone would light up every gradient. Diff it against ITSELF after a forced full
 * repaint (`redraw`); same buffer, same quantisation, and the only variable left is
 * how much LVGL was told to paint. */
int sim_shot_fb(const char *path)
{
    const uint16_t *fb = sim_fb();
    if (!fb) { fprintf(stderr, "shotfb: headless only\n"); return -1; }
    int w = SERIN_DISP, h = SERIN_DISP;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (!rgb) return -1;
    for (int i = 0; i < w * h; i++) {
        uint16_t p = fb[i];                       /* rgb565 */
        uint8_t r = (uint8_t)((p >> 11) & 0x1F), g = (uint8_t)((p >> 5) & 0x3F), b = (uint8_t)(p & 0x1F);
        rgb[3 * i]     = (uint8_t)((r * 255 + 15) / 31);
        rgb[3 * i + 1] = (uint8_t)((g * 255 + 31) / 63);
        rgb[3 * i + 2] = (uint8_t)((b * 255 + 15) / 31);
    }
    int ok = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    if (!ok) { fprintf(stderr, "shotfb: cannot write %s\n", path); return -1; }
    printf("shotfb: %s (%dx%d)\n", path, w, h);
    return 0;
}

int sim_shot(const char *path)
{
    lv_draw_buf_t *scr = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
    if (!scr) { fprintf(stderr, "shot: snapshot failed\n"); return -1; }
    lv_draw_buf_t *top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);

    int w = scr->header.w, h = scr->header.h;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    if (!rgb) { lv_draw_buf_destroy(scr); if (top) lv_draw_buf_destroy(top); return -1; }

    for (int y = 0; y < h; y++) {
        const uint8_t *s = (const uint8_t *)scr->data + (size_t)y * scr->header.stride;
        const uint8_t *t = top ? (const uint8_t *)top->data + (size_t)y * top->header.stride : NULL;
        for (int x = 0; x < w; x++) {
            uint8_t b = s[4 * x], g = s[4 * x + 1], r = s[4 * x + 2];   /* XRGB8888 = B,G,R,X */
            if (t) {
                uint8_t a = t[4 * x + 3];
                if (a) {
                    b = (uint8_t)((t[4 * x]     * a + b * (255 - a)) / 255);
                    g = (uint8_t)((t[4 * x + 1] * a + g * (255 - a)) / 255);
                    r = (uint8_t)((t[4 * x + 2] * a + r * (255 - a)) / 255);
                }
            }
            size_t o = 3 * ((size_t)y * w + x);
            rgb[o] = r; rgb[o + 1] = g; rgb[o + 2] = b;
        }
    }
    int ok = stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    lv_draw_buf_destroy(scr);
    if (top) lv_draw_buf_destroy(top);
    if (!ok) { fprintf(stderr, "shot: cannot write %s\n", path); return -1; }
    printf("shot: %s (%dx%d)\n", path, w, h);
    return 0;
}
