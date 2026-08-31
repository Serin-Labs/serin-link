/*******************************************************************************
 * Size: 56 px
 * Bpp: 4
 * Opts: --font /tmp/PlusJakartaSans-Medium.ttf --size 56 --bpp 4 --format lvgl --no-compress --force-fast-kern-format --range 0x00B0 -o jakarta_500_56.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef JAKARTA_500_56
#define JAKARTA_500_56 1
#endif

#if JAKARTA_500_56

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+00B0 "°" */
    0x0, 0x0, 0x0, 0x5a, 0xdf, 0xfd, 0xa5, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x3d, 0xff, 0xff, 0xff,
    0xff, 0xd2, 0x0, 0x0, 0x0, 0x5, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x50, 0x0, 0x0, 0x3f,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf2, 0x0,
    0x0, 0xef, 0xff, 0xf9, 0x30, 0x3, 0xaf, 0xff,
    0xfd, 0x0, 0x7, 0xff, 0xff, 0x50, 0x0, 0x0,
    0x6, 0xff, 0xff, 0x60, 0xc, 0xff, 0xf9, 0x0,
    0x0, 0x0, 0x0, 0x9f, 0xff, 0xb0, 0xf, 0xff,
    0xf2, 0x0, 0x0, 0x0, 0x0, 0x3f, 0xff, 0xf0,
    0x1f, 0xff, 0xf0, 0x0, 0x0, 0x0, 0x0, 0xf,
    0xff, 0xf0, 0x1f, 0xff, 0xf0, 0x0, 0x0, 0x0,
    0x0, 0xf, 0xff, 0xf0, 0xf, 0xff, 0xf2, 0x0,
    0x0, 0x0, 0x0, 0x2f, 0xff, 0xf0, 0xc, 0xff,
    0xf9, 0x0, 0x0, 0x0, 0x0, 0x9f, 0xff, 0xb0,
    0x7, 0xff, 0xff, 0x50, 0x0, 0x0, 0x6, 0xff,
    0xff, 0x60, 0x0, 0xef, 0xff, 0xfa, 0x41, 0x14,
    0xbf, 0xff, 0xfd, 0x0, 0x0, 0x3f, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xf2, 0x0, 0x0, 0x5,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x50, 0x0,
    0x0, 0x0, 0x3d, 0xff, 0xff, 0xff, 0xff, 0xd2,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x5a, 0xef, 0xfe,
    0xa5, 0x0, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 448, .box_w = 20, .box_h = 18, .ofs_x = 4, .ofs_y = 24}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 176, .range_length = 1, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t jakarta_500_56 = {
#else
lv_font_t jakarta_500_56 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 18,          /*The maximum line height required by the font*/
    .base_line = -24,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -9,
    .underline_thickness = 2,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if JAKARTA_500_56*/

