// screen_presetbrowser.h
#pragma once
#include "guiscreenbase.h"
#include "ui_widgets.h"
#include "guicontroller.h"
#include <vector>
#include <string>

class ScreenPresetBrowser : public GUIScreenBase {
public:
    ScreenPresetBrowser(DisplayType type);

    void Build(lv_obj_t* root, const GUILayout& layout) override;
    void Update() override;
    void OnEvent(UIEvent ev) override;

private:
    void LoadCurrentPreset();

private:
    struct PresetData {
        PresetInfo info;
        int index; // Preset index
    };

private:
    DisplayType m_Type;
    std::vector<PresetData> m_Presets;
    IHeader*    m_Header = nullptr;
    IListView*  m_List   = nullptr;
    int         m_Selected = 0;
};
