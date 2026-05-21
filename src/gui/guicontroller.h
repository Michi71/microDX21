// guicontroller.h
#pragma once
#include <lvgl/lvgl.h>
#include "guilayout.h"
#include "ui_types.h"
#include "velvetkeys.h"

class GUIScreenBase;

class GUIController {
public:
    GUIController(unsigned width, unsigned height, DisplayType type);
    ~GUIController();

    void Init();
    void ShowScreen(GUIScreenBase* screen);
    void Update();

    // von außen (Encoder, Buttons, etc.)
    void HandleEvent(UIEvent ev);

    // Navigation
    void NavigateToSplash();
    void NavigateToPresetBrowser();
    void NavigateToParameter();

    // Encoder events
    void OnEncoderCW();    // optional, falls du sie später brauchst
    void OnEncoderCCW();   // optional
    void OnEncoderClick();
    void OnEncoderBack();

    void SetVelvetKeys(CVelvetKeys* vk) { m_VelvetKeys = vk; }
    CVelvetKeys* GetVelvetKeys() const { return m_VelvetKeys; }

    // Touch-Input-Device initialisieren
    void InitTouchInput();

private:
    GUILayout SelectLayout(unsigned w, unsigned h);

private:
    unsigned       m_Width;
    unsigned       m_Height;
    DisplayType    m_Type;

    GUILayout      m_Layout;
    GUIScreenBase* m_CurrentScreen = nullptr;
    GUIScreenBase* m_PendingScreen = nullptr;
    lv_obj_t*      m_Root          = nullptr;

    CVelvetKeys*   m_VelvetKeys = nullptr;

    lv_indev_t*    m_TouchIndev = nullptr;
};
