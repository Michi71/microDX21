// screen_parameter.cpp
#include "screen_parameter.h"
#include "ui_widget_factory.h"
#include "guicontroller.h"
#include "velvetkeys.h"
#include "opmemuadapter.h"

#include <algorithm>
#include <string>

// ──────────────────────────────────────────────────────────────────
// Parameter index → group / short name.
//
// DX21 FM synthesizer parameters — replaces the former vkSynth
// parameter indices (piano sample engine + effects chain).
// ──────────────────────────────────────────────────────────────────

namespace
{

const char* GroupNameForIndex(int idx)
{
    if (idx >= kParamMasterGain    && idx <= kParamPortamentoRate)  return "Global";
    if (idx >= kParamLFOSpeed      && idx <= kParamAMD)             return "LFO";
    if (idx >= kParamAlgorithm     && idx <= kParamLFOWave)         return "Voice";
    if (idx >= kParamOp0AR         && idx <= kParamOp0CRS)          return "OP1";
    if (idx >= kParamOp1AR         && idx <= kParamOp1CRS)          return "OP2";
    if (idx >= kParamOp2AR         && idx <= kParamOp2CRS)          return "OP3";
    if (idx >= kParamOp3AR         && idx <= kParamOp3CRS)          return "OP4";
    return "?";
}

const char* ShortNameForIndex(int idx)
{
    static const char* kShortNames[kParamTotalCount] = {
        // Global (0-7)
        "Gain", "Ensemble", "PlayMode", "SplitPt",
        "Balance", "PBRange", "PortaMode", "PortaRate",
        // LFO (8-11)
        "LFOspd", "LFOdel", "PMD", "AMD",
        // Voice (12-15)
        "Algorithm", "Feedback", "LFOSync", "LFOWave",
        // OP0/OP1 (16-25)
        "AR", "D1R", "D1L", "Out", "CRS",
        "AR", "D1R", "D1L", "Out", "CRS",
        // OP2/OP3 (26-35)
        "AR", "D1R", "D1L", "Out", "CRS",
        "AR", "D1R", "D1L", "Out", "CRS",
    };
    if (idx < 0 || idx >= kParamTotalCount) return "?";
    return kShortNames[idx];
}

} // namespace

// ──────────────────────────────────────────────────────────────────

ScreenParameter::ScreenParameter(DisplayType type)
: m_Type(type)
{
}

void ScreenParameter::Build(lv_obj_t* root, const GUILayout& layout)
{
    m_Header = UIWidgetFactory::CreateHeader(m_Type);
    m_Param  = UIWidgetFactory::CreateParam(m_Type);

    if (m_Header)
        m_Header->AttachTo(root);

    if (!m_Param)
    {
        UpdateHeaderMode();
        return;
    }

    m_Param->AttachTo(root);

    // ─── Encoder rotation in BROWSE mode → scroll through parameters ───
    m_Param->OnScroll = [this](int delta)
    {
        if (!m_GUI) return;

        const int total = (int)kParamTotalCount;
        const int newIndex = std::clamp(m_ParamIndex + delta, 0, total - 1);
        if (newIndex == m_ParamIndex)
            return;

        m_ParamIndex = newIndex;
        UpdateParamDisplay();
    };

    // ─── Encoder rotation in EDIT mode → write parameter to synth ───
    m_Param->OnValueChanged = [this](int value)
    {
        if (!m_GUI || !m_EditMode) return;
        CVelvetKeys* vk = m_GUI->GetVelvetKeys();
        if (!vk) return;

        vk->SetParameter(m_ParamIndex, (float)value / 100.0f);
    };

    // ─── Encoder click → toggle BROWSE / EDIT ───
    m_Param->OnEnter = [this]()
    {
        if (m_EditMode) ExitEditMode();
        else            EnterEditMode();
    };

    // Initial state: browse, parameter 0
    m_EditMode   = false;
    m_ParamIndex = 0;
    m_Param->SetEditMode(false);
    UpdateHeaderMode();
    UpdateParamDisplay();
}

void ScreenParameter::UpdateParamDisplay()
{
    if (!m_GUI || !m_Param) return;
    CVelvetKeys* vk = m_GUI->GetVelvetKeys();
    if (!vk) return;

    // Group + parameter name
    m_Param->SetGroup(GroupNameForIndex(m_ParamIndex));
    m_Param->SetLabel(ShortNameForIndex(m_ParamIndex));

    // Current value as integer percent (round-half-up)
    const float norm = vk->GetParameter(m_ParamIndex);
    const int pct = (int)(std::clamp(norm, 0.0f, 1.0f) * 100.0f + 0.5f);
    m_Param->SetValue(pct);
}

void ScreenParameter::UpdateHeaderMode()
{
    if (!m_Header) return;
    m_Header->SetTitle(m_EditMode ? "EDIT PARAM" : "BROWSE PARAM");
}

void ScreenParameter::EnterEditMode()
{
    m_EditMode = true;
    UpdateHeaderMode();
    if (m_Param) m_Param->SetEditMode(true);
    // Re-read the current value so the encoder starts from the
    // displayed value (in case it drifted from another source like CC).
    UpdateParamDisplay();
}

void ScreenParameter::ExitEditMode()
{
    m_EditMode = false;
    UpdateHeaderMode();
    if (m_Param) m_Param->SetEditMode(false);
    UpdateParamDisplay();
}

void ScreenParameter::OnEvent(UIEvent ev)
{
    if (!m_GUI) return;

    switch (ev)
    {
        case UIEvent::EncoderBack:
            // Long press: back to Preset Browser regardless of mode.
            m_GUI->NavigateToPresetBrowser();
            break;

        default:
            break;
    }
}
