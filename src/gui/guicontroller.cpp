// guicontroller.cpp
#include "guicontroller.h"
#include "encoder.h"
#include "guiscreenbase.h"
#include "screen_splash.h"
#include "screen_presetbrowser.h"
#include "screen_parameter.h"

GUIController::GUIController(unsigned w, unsigned h, DisplayType type)
: m_Width(w), m_Height(h), m_Type(type)
{
    m_Layout = SelectLayout(w, h);
}

// Externer Touch-Read-Callback (definiert in displaymanager.cpp)
extern void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

void GUIController::Init()
{
    m_Root = lv_obj_create(nullptr);
    lv_scr_load(m_Root);

    // Fokusgruppe
    lv_group_t* g = lv_group_create();
    lv_group_set_default(g);

    // Encoder-Indev erzeugen
    lv_indev_t* encoder = encoder_init_lvgl();

    // Encoder der Gruppe zuweisen
    lv_indev_set_group(encoder, g);
}

void GUIController::InitTouchInput()
{
    if (m_TouchIndev)
        return;

    m_TouchIndev = lv_indev_create();
    lv_indev_set_type(m_TouchIndev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(m_TouchIndev, touch_read_cb);
}

void GUIController::OnEncoderCW()
{
    // aktuell ungenutzt – LVGL bekommt enc_diff direkt
}

void GUIController::OnEncoderCCW()
{
    // aktuell ungenutzt
}

void GUIController::OnEncoderClick()
{
    // Send to LVGL group for focused widgets
    lv_group_t* g = lv_group_get_default();
    if (g)
        lv_group_send_data(g, LV_KEY_ENTER);

    // Also notify current screen for special handling (e.g. splash screen)
    if (m_CurrentScreen)
        m_CurrentScreen->OnEvent(UIEvent::EncoderClick);
}

void GUIController::OnEncoderBack()
{
    if (m_CurrentScreen)
        m_CurrentScreen->OnEvent(UIEvent::EncoderBack);
}

GUILayout GUIController::SelectLayout(unsigned w, unsigned h)
{
    GUILayout g{};

    if (w <= 128 && h <= 32)
    {
        g.fontSize   = 8;
        g.padding    = 0;
        g.showHeader = false;
        g.showFooter = false;
        g.maxLines   = 1;
        g.fgColor    = lv_color_white();
        g.bgColor    = lv_color_black();
    }
    else if (w <= 128 && h <= 64)
    {
        g.fontSize   = 8;
        g.padding    = 2;
        g.showHeader = true;
        g.showFooter = false;
        g.maxLines   = 3;
        g.fgColor    = lv_color_white();
        g.bgColor    = lv_color_black();
    }
    else if (w <= 320)
    {
        g.fontSize   = 18;
        g.padding    = 6;
        g.showHeader = true;
        g.showFooter = true;
        g.maxLines   = 10;
        g.fgColor    = lv_color_black();
        g.bgColor    = lv_color_white();
    }
    else
    {
        g.fontSize   = 22;
        g.padding    = 10;
        g.showHeader = true;
        g.showFooter = true;
        g.maxLines   = 20;
        g.fgColor    = lv_color_black();
        g.bgColor    = lv_color_white();
    }

    return g;
}

GUIController::~GUIController()
{
    delete m_CurrentScreen;
    m_CurrentScreen = nullptr;
    delete m_PendingScreen;
    m_PendingScreen = nullptr;
}

void GUIController::ShowScreen(GUIScreenBase* screen)
{
    delete m_PendingScreen;
    m_PendingScreen = screen;
}

void GUIController::Update()
{
    if (m_PendingScreen)
    {
        delete m_CurrentScreen;
        m_CurrentScreen = m_PendingScreen;
        m_PendingScreen = nullptr;

        if (m_CurrentScreen)
        {
            m_CurrentScreen->SetGUI(this);
            lv_obj_clean(m_Root);
            m_CurrentScreen->Build(m_Root, m_Layout);
        }
    }

    if (m_CurrentScreen)
        m_CurrentScreen->Update();
}

void GUIController::HandleEvent(UIEvent ev)
{
    if (m_CurrentScreen)
        m_CurrentScreen->OnEvent(ev);
}

void GUIController::NavigateToSplash()
{
    ShowScreen(new ScreenSplash(m_Type));
}

void GUIController::NavigateToPresetBrowser()
{
    ShowScreen(new ScreenPresetBrowser(m_Type));
}

void GUIController::NavigateToParameter()
{
    ShowScreen(new ScreenParameter(m_Type));
}

