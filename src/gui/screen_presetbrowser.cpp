// screen_presetbrowser.cpp
#include "screen_presetbrowser.h"
#include "ui_widget_factory.h"
#include "velvetkeys.h"
#include <circle/logger.h>

static const char FromScreenPreset[] = "screen_presetbrowser";

ScreenPresetBrowser::ScreenPresetBrowser(DisplayType type)
: m_Type(type)
{
}

void ScreenPresetBrowser::Build(lv_obj_t* root, const GUILayout& layout)
{
    m_Header = UIWidgetFactory::CreateHeader(m_Type);
    m_List   = UIWidgetFactory::CreateList(m_Type);

    if (!m_Header || !m_List)
        return;

    m_Header->AttachTo(root);
    m_Header->SetTitle("PRESETS");

    // Presets aus VelvetKeys holen
    CVelvetKeys* vk = m_GUI->GetVelvetKeys();

    if (vk != nullptr)
    {
        int count = vk->GetNumPresets();
        m_Presets.clear();
        m_Presets.reserve(count);

        for (int i = 0; i < count && m_Presets.size() < 50; i++) // Limit to 50 presets
        {
            const PresetInfo* p = vk->GetPreset(i);
            if (p && !p->name.empty())
                m_Presets.push_back({*p, i}); // Store preset info and index
        }

        // PresetData → IconListItem
        std::vector<IconListItem> items;
        items.reserve(m_Presets.size());

        for (const auto& p : m_Presets)
        {
            std::string name = p.info.name.substr(0, 20); // Truncate name to 20 chars
            std::string category = p.info.category.substr(0, 20); // Truncate category to 20 chars
            items.push_back({ name, category, MY_SYMBOL_PRESET, p.index });
        }

        m_List->AttachTo(root);
        m_List->SetItems(items);

        // Aktuelles Preset vorselektieren
        int currentPreset = vk->GetInstrument();
        // Find the index in m_Presets that corresponds to currentPreset
        for (int i = 0; i < (int)m_Presets.size(); i++)
        {
            if (m_Presets[i].index == currentPreset)
            {
                m_Selected = i;
                break;
            }
        }

        m_List->SetSelectedIndex(m_Selected);
    }

    // Kurzer Tastendruck → Preset laden
    m_List->OnItemSelected = [this](int index)
    {
        m_Selected = index;
        LoadCurrentPreset();
    };
}

void ScreenPresetBrowser::Update()
{
    // Aktuell nichts zu tun
}

void ScreenPresetBrowser::LoadCurrentPreset()
{
    if (!m_GUI) return;

    CVelvetKeys* vk = m_GUI->GetVelvetKeys();
    if (!vk) return;

    if (m_List)
        m_Selected = m_List->GetSelectedIndex();

    // Strict bounds check
    if (m_Selected < 0 || m_Selected >= (int)m_Presets.size() || m_Presets.empty())
    {
        CLogger::Get()->Write(FromScreenPreset, LogError,
                "ScreenPresetBrowser: invalid selection (selected=%d, count=%zu, empty=%d)",
                m_Selected, m_Presets.size(), m_Presets.empty() ? 1 : 0);
        return;
    }

    int presetIndex = m_Presets[m_Selected].index;
    CLogger::Get()->Write(FromScreenPreset, LogNotice,
            "ScreenPresetBrowser: loading preset index %d (selected=%d, count=%zu)",
            presetIndex, m_Selected, m_Presets.size());

    if (m_Header)
    {
        std::string title = "LOADING: " + m_Presets[m_Selected].info.name;
        m_Header->SetTitle(title);
    }

    vk->LoadPreset(presetIndex); // Deferred: sets flag, Core 0 loads on next Process()

    if (m_Header)
    {
        std::string title = m_Presets[m_Selected].info.name;
        m_Header->SetTitle(title);
    }
}

void ScreenPresetBrowser::OnEvent(UIEvent ev)
{
    if (!m_GUI) return;

    switch (ev)
    {
        case UIEvent::EncoderBack:
            // Langer Tastendruck → Parameter Editor
            m_GUI->NavigateToParameter();
            break;

        default:
            break;
    }
}