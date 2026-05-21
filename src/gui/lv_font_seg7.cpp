/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --bpp 1 --size 16 --font /Users/michael/SquareLine/assets/DSEG7-Classic-Bold-modified.ttf -o /Users/michael/SquareLine/assets/ui_font_seg7.c --format lvgl -r 0x20-0xff --no-compress --no-prefilter
 ******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl/lvgl.h>
#include "lv_font_seg7.h"

#ifndef LV_FONT_SEG7
#define LV_FONT_SEG7 1
#endif

#if LV_FONT_SEG7

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+002D "-" */
    0xff, 0xfc,

    /* U+002E "." */
    0xf0,

    /* U+0030 "0" */
    0xff, 0xff, 0xf0, 0x78, 0x3c, 0x1e, 0xf, 0x7,
    0x1, 0x80, 0xe0, 0xf0, 0x78, 0x3c, 0x1e, 0xf,
    0xff, 0xff,

    /* U+0031 "1" */
    0x7f, 0xf5, 0xff, 0xd0,

    /* U+0032 "2" */
    0xff, 0xbf, 0xc0, 0x60, 0x30, 0x18, 0xc, 0x6,
    0xff, 0xff, 0x60, 0x30, 0x18, 0xc, 0x6, 0x3,
    0xfd, 0xff,

    /* U+0033 "3" */
    0xff, 0xbf, 0xc0, 0x60, 0x30, 0x18, 0xc, 0x6,
    0xff, 0x7f, 0x80, 0xc0, 0x60, 0x30, 0x18, 0xd,
    0xff, 0xff,

    /* U+0034 "4" */
    0x80, 0xe0, 0xf0, 0x78, 0x3c, 0x1e, 0xf, 0xfe,
    0xff, 0x1, 0x80, 0xc0, 0x60, 0x30, 0x18, 0x4,

    /* U+0035 "5" */
    0xff, 0xff, 0xb0, 0x18, 0xc, 0x6, 0x3, 0x1,
    0xfe, 0x7f, 0x80, 0xc0, 0x60, 0x30, 0x18, 0xd,
    0xff, 0xff,

    /* U+0036 "6" */
    0xff, 0xff, 0xb0, 0x18, 0xc, 0x6, 0x3, 0x1,
    0xfe, 0xff, 0xe0, 0xf0, 0x78, 0x3c, 0x1e, 0xf,
    0xff, 0xff,

    /* U+0037 "7" */
    0xff, 0xbf, 0xc0, 0x60, 0x30, 0x18, 0xc, 0x6,
    0x1, 0x0, 0x80, 0xc0, 0x60, 0x30, 0x18, 0xc,
    0x2,

    /* U+0038 "8" */
    0xff, 0xff, 0xf0, 0x78, 0x3c, 0x1e, 0xf, 0x7,
    0xff, 0xff, 0xe0, 0xf0, 0x78, 0x3c, 0x1e, 0xf,
    0xff, 0xff,

    /* U+0039 "9" */
    0xff, 0xff, 0xf0, 0x78, 0x3c, 0x1e, 0xf, 0x7,
    0xff, 0x7f, 0x80, 0xc0, 0x60, 0x30, 0x18, 0xd,
    0xff, 0xff,

    /* U+003A ":" */
    0xe0, 0x3, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 51, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 209, .box_w = 7, .box_h = 2, .ofs_x = 3, .ofs_y = 7},
    {.bitmap_index = 3, .adv_w = 0, .box_w = 2, .box_h = 2, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 22, .adv_w = 209, .box_w = 2, .box_h = 14, .ofs_x = 9, .ofs_y = 1},
    {.bitmap_index = 26, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 44, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 62, .adv_w = 209, .box_w = 9, .box_h = 14, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 78, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 114, .adv_w = 209, .box_w = 9, .box_h = 15, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 131, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 149, .adv_w = 209, .box_w = 9, .box_h = 16, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 167, .adv_w = 51, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = 3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0xd, 0xe
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 15, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 48, .range_length = 11, .glyph_id_start = 4,
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
    .cmap_num = 2,
    .bpp = 1,
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
const lv_font_t lv_font_seg7 = {
#else
lv_font_t lv_font_seg7 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 16,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_SEG7*/

#ifdef __cplusplus
} // extern "C"
#endif
