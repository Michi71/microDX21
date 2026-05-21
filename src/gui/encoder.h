#pragma once
#include <lvgl/lvgl.h>

// Von der Hardware aufzurufen:
void encoder_hw_delta(int delta);      // +1 / -1 vom Drehgeber
void encoder_hw_click();
void encoder_block_left(bool block);
bool get_encoder_block_left();
void encoder_block_right(bool block);
bool get_encoder_block_right();

// Von GUIController aufzurufen:
lv_indev_t* encoder_init_lvgl();       // erzeugt LVGL-Indev
