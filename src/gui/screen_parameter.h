#pragma once
#include "guiscreenbase.h"
#include "ui_widgets.h"

class ScreenParameter : public GUIScreenBase {
public:
    ScreenParameter(DisplayType type);

    void Build(lv_obj_t* root, const GUILayout& layout) override;
    void OnEvent(UIEvent ev) override;

private:
    // Refresh the widget with the data of the parameter at m_ParamIndex
    // (group label, name, current value).
    void UpdateParamDisplay();

    void UpdateHeaderMode();
    void EnterEditMode();
    void ExitEditMode();

private:
    DisplayType    m_Type;
    IHeader*       m_Header     = nullptr;
    IParamControl* m_Param      = nullptr;
    bool           m_EditMode   = false;   // false = browse, true = edit
    int            m_ParamIndex = 0;       // 0..kTotalParameters-1
};
