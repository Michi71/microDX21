// guiscreenbase.h
#pragma once
#include <lvgl/lvgl.h>
#include "guilayout.h"
#include "ui_types.h"

class GUIController; // Forward

class GUIScreenBase {
public:
    virtual ~GUIScreenBase() = default;

    virtual void Build(lv_obj_t* root, const GUILayout& layout) = 0;
    virtual void Update() {}
    virtual void OnEvent(UIEvent ev) {}

    void SetGUI(GUIController* gui) { m_GUI = gui; }

protected:
    GUIController* m_GUI = nullptr;
};
