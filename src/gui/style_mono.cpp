#include "style_mono.h"

lv_style_t style_mono_main;
lv_style_t style_mono_focus;

void vk_init_mono_styles()
{
    lv_style_init(&style_mono_main);
    lv_style_set_bg_color(&style_mono_main, lv_color_black());
    lv_style_set_text_color(&style_mono_main, lv_color_white());
    lv_style_set_border_width(&style_mono_main, 0);
    lv_style_set_radius(&style_mono_main, 0);
    lv_style_set_pad_all(&style_mono_main, 0);

    extern const lv_font_t lv_font_unscii_8;
    lv_style_set_text_font(&style_mono_main, &lv_font_unscii_8);

    lv_style_init(&style_mono_focus);
    lv_style_set_bg_color(&style_mono_focus, lv_color_white());
    lv_style_set_text_color(&style_mono_focus, lv_color_black());
}

void vk_apply_mono(lv_obj_t* obj)
{
    lv_obj_add_style(obj, &style_mono_main, LV_PART_MAIN);
    lv_obj_add_style(obj, &style_mono_focus, LV_PART_MAIN | LV_STATE_FOCUSED);

    lv_obj_add_style(obj, &style_mono_main, LV_PART_ITEMS);
    lv_obj_add_style(obj, &style_mono_focus, LV_PART_ITEMS | LV_STATE_FOCUSED);
}
