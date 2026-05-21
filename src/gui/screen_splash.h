// screen_splash.h
#pragma once
#include "guiscreenbase.h"
#include "ui_widgets.h"
#include <lvgl/lvgl.h>

class ScreenSplash : public GUIScreenBase {
public:
    ScreenSplash(DisplayType type);

    ~ScreenSplash() override;

    void Build(lv_obj_t* root, const GUILayout& layout) override;
    void Update() override;
    void OnEvent(UIEvent ev) override;

    static void OnTimerExpired(lv_timer_t* timer);

private:
    void CancelTimer();
    DisplayType m_Type;
    lv_obj_t*   m_Logo = nullptr;
    lv_obj_t*   m_Version = nullptr;
    lv_timer_t* m_Timer = nullptr;
    bool        m_Transitioned = false;

    static constexpr unsigned SPLASH_DURATION_MS = 2500; // 2.5 Sekunden
};
