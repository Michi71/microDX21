// guilayout.h
#pragma once
#include <lvgl/lvgl.h>

struct GUILayout
{
    int fontSize;
    int padding;
    int headerHeight;
    int footerHeight;
    bool showIcons;
    bool showHeader;
    bool showFooter;
    int maxLines;
    lv_color_t fgColor;
    lv_color_t bgColor;
};
