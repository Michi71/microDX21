// screen_splash.cpp
#include "screen_splash.h"
#include "guicontroller.h"
#include "ui_img_velvetkeys128x64_png.h"

ScreenSplash::ScreenSplash(DisplayType type)
: m_Type(type)
{
}

ScreenSplash::~ScreenSplash()
{
    CancelTimer();
}

void ScreenSplash::CancelTimer()
{
    if (m_Timer)
    {
        lv_timer_del(m_Timer);
        m_Timer = nullptr;
    }
}

void ScreenSplash::OnTimerExpired(lv_timer_t* timer)
{
    ScreenSplash* self = static_cast<ScreenSplash*>(lv_timer_get_user_data(timer));
    if (!self || self->m_Transitioned)
        return;

    self->m_Transitioned = true;
    self->CancelTimer();

    if (self->m_GUI)
        self->m_GUI->NavigateToPresetBrowser();
}

void ScreenSplash::Build(lv_obj_t* root, const GUILayout& layout)
{
    // Hintergrund schwarz
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);

    // Logo/Titel zentriert
    if (m_Type == DisplayType::Mono128x64)
    {
        m_Logo = lv_image_create(root);
        lv_image_set_src(m_Logo, &ui_img_velvetkeys128x64_png);
    }
    else
    {
        m_Logo = lv_label_create(root);
        lv_label_set_text(m_Logo, "VELVETKEYS");
        lv_obj_set_style_text_color(m_Logo, lv_color_white(), 0);

        // Groessere Schrift fuer Logo
        if (m_Type == DisplayType::Mono128x32)
        {
            lv_obj_set_style_text_font(m_Logo, &lv_font_montserrat_14, 0);
        }
        else
        {
            lv_obj_set_style_text_font(m_Logo, &lv_font_montserrat_28, 0);
        }
    }

    lv_obj_align(m_Logo, LV_ALIGN_CENTER, 0, 0);

    // Version unten
    m_Version = lv_label_create(root);
    lv_label_set_text(m_Version, "v1.0.0");
    lv_obj_set_style_text_color(m_Version, lv_color_white(), 0);

    if (m_Type == DisplayType::Mono128x64 || m_Type == DisplayType::Mono128x32)
    {
        lv_obj_set_style_text_font(m_Version, &lv_font_montserrat_10, 0);
        lv_obj_align(m_Version, LV_ALIGN_BOTTOM_MID, 0, -5);
    }
    else
    {
        lv_obj_set_style_text_font(m_Version, &lv_font_montserrat_14, 0);
        lv_obj_align(m_Version, LV_ALIGN_BOTTOM_MID, 0, -20);
    }

    // LVGL-Timer: automatischer Screen-Wechsel nach SPLASH_DURATION_MS
    m_Timer = lv_timer_create(ScreenSplash::OnTimerExpired, SPLASH_DURATION_MS, this);
    lv_timer_set_repeat_count(m_Timer, 1); // nur einmal
}

void ScreenSplash::Update()
{
    // nichts mehr zu tun – Timer erledigt den Wechsel
}

void ScreenSplash::OnEvent(UIEvent ev)
{
    // Jeder Tastendruck ueberspringt den Splash
    if (ev == UIEvent::EncoderClick && !m_Transitioned)
    {
        m_Transitioned = true;
        CancelTimer();
        if (m_GUI)
            m_GUI->NavigateToPresetBrowser();
    }
}
