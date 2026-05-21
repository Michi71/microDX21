#pragma once
#include <lvgl/lvgl.h>

LV_FONT_DECLARE(lv_font_unscii_16);

// Styles global sichtbar machen
extern lv_style_t style_mono_main;
extern lv_style_t style_mono_focus;

void vk_init_mono_styles();
void vk_apply_mono(lv_obj_t* obj);
