// ui_widgets_mono.h
#pragma once
#include "ui_widgets.h"
#include "lv_font_icons.h"
#include "lv_font_seg7.h"
#include <vector>
#include <string>
#include <functional>

class MonoHeader : public IHeader
{
public:
    MonoHeader() = default;

    void AttachTo(lv_obj_t* parent) override;
    void SetTitle(const std::string& title) override;

private:
    lv_obj_t* m_Label = nullptr;
};

class MonoListView : public IListView {
public:
    void AttachTo(lv_obj_t* parent) override;
    void SetItems(const std::vector<IconListItem>& items) override;
    void SetSelectedIndex(int index) override;
    int  GetSelectedIndex() const override;

private:
    void UpdateArrows();
    
    lv_obj_t* m_Roller = nullptr;
    lv_obj_t* m_MarkerLeft = nullptr;
    lv_obj_t* m_MarkerRight = nullptr;
    int m_CurrentIndex = 0;
    int m_ItemCount = 0;
};

class MonoParamControl : public IParamControl
{
public:
    MonoParamControl() = default;

    void AttachTo(lv_obj_t* parent) override;
    void SetLabel(const std::string& label) override;
    void SetGroup(const std::string& group) override;
    void SetValue(int value) override;
    void SetEditMode(bool editing) override;
    void SetEncoderEnabled(bool enabled) override;

    void SetRange(int min, int max, int step);
    int  GetValue() const { return m_CurrentValue; }
    int  GetMin()   const { return m_MinValue; }
    int  GetMax()   const { return m_MaxValue; }
    int  GetStep()  const { return m_Step; }
    bool IsEditing() const { return m_Editing; }

private:
    void UpdateValueDisplay();

    lv_obj_t* m_Container        = nullptr;
    lv_obj_t* m_Bar              = nullptr;
    lv_obj_t* m_ValueLabel       = nullptr;
    lv_obj_t* m_ParamLabel       = nullptr;
    lv_obj_t* m_ParamGroupLabel  = nullptr;

    int  m_CurrentValue = 0;
    int  m_MinValue     = 0;
    int  m_MaxValue     = 100;
    int  m_Step         = 1;
    bool m_Editing      = false;     // false = browse, true = edit
};
