#include "ui_widgets_mono.h"
#include "style_mono.h"
#include <lvgl/lvgl.h>
#include <string>
#include <numeric>
#include "encoder.h"

extern const lv_font_t lv_font_unscii_8;
extern const lv_font_t lv_font_unscii_16;

void MonoHeader::AttachTo(lv_obj_t* parent)
{
    lv_obj_remove_flag          ( parent, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
    lv_obj_set_style_bg_color   ( parent, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT );

    // Label
    m_Label = lv_label_create(parent);
    lv_obj_set_height( m_Label, 10);
    lv_obj_set_width( m_Label, lv_pct(100));
    lv_obj_set_align( m_Label, LV_ALIGN_TOP_MID );

    vk_apply_mono(m_Label);

    // Trennlinie
    lv_obj_set_style_border_color   (m_Label, lv_color_white(),      LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa     (m_Label, 255,                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width   (m_Label, 1,                     LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side    (m_Label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void MonoHeader::SetTitle(const std::string& title)
{
    if (m_Label)
        lv_label_set_text(m_Label, title.c_str());
}

static void roller_event_cb(lv_event_t* e)
{
    MonoListView* self = static_cast<MonoListView*>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        int current = self->GetSelectedIndex();
        lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
        int total = (int)lv_obj_get_child_count(target);

        if (key == LV_KEY_RIGHT) {
            if (current < total - 1) 
                self->SetSelectedIndex(current + 1);
        }
        else if (key == LV_KEY_LEFT) {
            if (current > 0) 
                self->SetSelectedIndex(current - 1);
        }
        else if (key == LV_KEY_ENTER) {
            if (self->OnItemSelected)
                self->OnItemSelected(current);
        }
    }
}

void MonoListView::AttachTo(lv_obj_t* parent)
{
    // Create left arrow marker
    m_MarkerLeft = lv_label_create(parent);
    lv_obj_set_size(m_MarkerLeft, 8, 8);
    lv_obj_set_y(m_MarkerLeft, 38);
    lv_obj_set_style_text_font(m_MarkerLeft, &lv_font_unscii_8, LV_PART_MAIN);
    lv_label_set_text(m_MarkerLeft, "<"); 
    lv_obj_set_style_text_color(m_MarkerLeft, lv_color_white(), 0);
    lv_obj_align(m_MarkerLeft, LV_ALIGN_LEFT_MID, 0, 0); 

    // Create right arrow marker
    m_MarkerRight = lv_label_create(parent);
    lv_obj_set_size(m_MarkerRight, 8, 8);
    lv_obj_set_y(m_MarkerRight, 38);
    lv_obj_set_style_text_font(m_MarkerRight, &lv_font_unscii_8, LV_PART_MAIN);
    lv_label_set_text(m_MarkerRight, ">"); 
    lv_obj_set_style_text_color(m_MarkerRight, lv_color_white(), 0);
    lv_obj_align(m_MarkerRight, LV_ALIGN_RIGHT_MID, 0, 0);

    // Create horizontal scrolling container
    m_Roller = lv_obj_create(parent);
    lv_obj_set_size(m_Roller, 112, 54); 
    lv_obj_set_y(m_Roller, 10);
    lv_obj_set_align(m_Roller, LV_ALIGN_CENTER);

    // Container als klickbar/fokussierbar markieren
    lv_obj_add_flag(m_Roller, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m_Roller, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // Flex-Layout for horizontal list
    lv_obj_set_flex_flow(m_Roller, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_Roller, LV_FLEX_ALIGN_CENTER, 
                                    LV_FLEX_ALIGN_CENTER, 
                                    LV_FLEX_ALIGN_CENTER);
    
    // Enable snapping for "roller feel"
    lv_obj_set_scroll_snap_x(m_Roller, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(m_Roller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(m_Roller, LV_DIR_HOR);
    lv_obj_clear_flag(m_Roller, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Styling
    lv_obj_set_style_text_font(m_Roller, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_Roller, lv_color_black(), 0);
    lv_obj_set_style_border_width(m_Roller, 0, 0);
    lv_obj_set_style_pad_all(m_Roller, 0, 0);
    lv_obj_set_style_pad_left(m_Roller, 40, 0);  // Space for first item to center
    lv_obj_set_style_pad_right(m_Roller, 40, 0); // Space for last item to center
    lv_obj_set_style_pad_column(m_Roller, 15, 0); // Space between items
    lv_obj_set_style_anim_duration(m_Roller, 150, 0); // Quick animation

    // Event handlers
    lv_obj_add_event_cb(m_Roller, roller_event_cb, LV_EVENT_KEY, this);

    // Add to focus group
    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, m_Roller);
        lv_group_focus_obj(m_Roller);
    }
}

void MonoListView::SetItems(const std::vector<IconListItem>& items)
{
    lv_obj_clean(m_Roller);
    m_ItemCount = items.size();

    for (const auto& item : items) {

        lv_obj_t* btn = lv_button_create(m_Roller);
        lv_obj_set_size(btn, 112, 40);

        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 4, 0);
        lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUS_KEY);

        // --- ICON ---
        lv_obj_t* icon = nullptr;
        if (item.icon && item.icon[0] != '\0') {
            icon = lv_label_create(btn);
            lv_obj_set_style_text_font(icon, &lv_font_icons, 0);
            lv_label_set_text(icon, item.icon);
            lv_obj_set_style_text_color(icon, lv_color_white(), 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);
        }

        int text_x = icon ? 22 : 0; // Platz für Icon

        // --- HAUPTTEXT ---
        lv_obj_t* label1 = lv_label_create(btn);
        lv_label_set_text(label1, item.text.c_str());
        lv_obj_set_style_text_color(label1, lv_color_white(), 0);
        lv_obj_set_style_text_font(label1, &lv_font_unscii_8, 0);

        lv_label_set_long_mode(label1, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(label1, 80);

        // --- SUBTEXT ---
        if (!item.subtext.empty()) {
            lv_obj_align(label1, LV_ALIGN_TOP_LEFT, text_x, 0);

            lv_obj_t* label2 = lv_label_create(btn);
            lv_label_set_text(label2, item.subtext.c_str());
            lv_obj_set_style_text_color(label2, lv_color_white(), 0);
            lv_obj_set_style_text_font(label2, &lv_font_unscii_8, 0);

            lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_width(label2, 80);

            lv_obj_align(label2, LV_ALIGN_BOTTOM_LEFT, text_x, 0);
        } else lv_obj_align(label1, LV_ALIGN_LEFT_MID, text_x, 0); // Zentrieren
    }

    lv_obj_update_layout(m_Roller);
    SetSelectedIndex(0);
}

void MonoListView::SetSelectedIndex(int index)
{
    lv_obj_update_layout(m_Roller);
    uint32_t cnt = lv_obj_get_child_count(m_Roller);
    if (index < 0 || index >= (int)cnt) return;

    // Clear previous selection
    for (uint32_t i = 0; i < cnt; ++i)
        lv_obj_clear_state(lv_obj_get_child(m_Roller, i), LV_STATE_CHECKED);

    // Set new selection
    lv_obj_t* child = lv_obj_get_child(m_Roller, index);
    lv_obj_add_state(child, LV_STATE_CHECKED);
    lv_obj_scroll_to_view(child, LV_ANIM_ON);
    
    m_CurrentIndex = index;
    UpdateArrows();
}

int MonoListView::GetSelectedIndex() const
{
    return m_CurrentIndex;
}

void MonoListView::UpdateArrows()
{
    // Hide left arrow if at first item
    if (m_CurrentIndex == 0) {
        lv_obj_add_flag(m_MarkerLeft, LV_OBJ_FLAG_HIDDEN);
        encoder_block_left(true);
    } else {
        lv_obj_clear_flag(m_MarkerLeft, LV_OBJ_FLAG_HIDDEN);
        encoder_block_left(false);
    }
    
    // Hide right arrow if at last item
    if (m_CurrentIndex >= m_ItemCount - 1) {
        lv_obj_add_flag(m_MarkerRight, LV_OBJ_FLAG_HIDDEN);
        encoder_block_right(true);
    } else {
        lv_obj_clear_flag(m_MarkerRight, LV_OBJ_FLAG_HIDDEN);
        encoder_block_right(false);
    }
}

static void param_event_cb(lv_event_t* e)
{
    MonoParamControl* self = static_cast<MonoParamControl*>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);

    if (code != LV_EVENT_KEY)
        return;

    uint32_t key = lv_event_get_key(e);

    // ENTER (encoder click) always toggles browse/edit via OnEnter —
    // the screen owns the mode flag and calls SetEditMode() back.
    if (key == LV_KEY_ENTER)
    {
        if (self->OnEnter)
            self->OnEnter();
        return;
    }

    const bool incr = (key == LV_KEY_RIGHT || key == LV_KEY_UP);
    const bool decr = (key == LV_KEY_LEFT  || key == LV_KEY_DOWN);
    if (!incr && !decr)
        return;

    const int delta = incr ? +1 : -1;

    if (self->IsEditing())
    {
        // ─── EDIT MODE: encoder changes the parameter value ───
        const int step    = self->GetStep();
        const int minVal  = self->GetMin();
        const int maxVal  = self->GetMax();
        const int current = self->GetValue();
        const int newValue = current + delta * step;

        if (newValue < minVal || newValue > maxVal)
            return;                     // clamp at boundary, no event
        if (newValue == current)
            return;

        self->SetValue(newValue);       // visual update only

        if (self->OnValueChanged)
            self->OnValueChanged(newValue);
    }
    else
    {
        // ─── BROWSE MODE: encoder scrolls through the parameter list ───
        if (self->OnScroll)
            self->OnScroll(delta);
    }
}

void MonoParamControl::AttachTo(lv_obj_t* parent)
{
    // Main container
    m_Container = lv_obj_create(parent);
    lv_obj_set_size(m_Container, lv_pct(100), 54);
    lv_obj_set_y(m_Container, 10);
    lv_obj_set_align(m_Container, LV_ALIGN_TOP_MID);
    
    // Style the container
    lv_obj_set_style_bg_color(m_Container, lv_color_black(), 0);
    lv_obj_set_style_border_width(m_Container, 0, 0);
    lv_obj_set_style_radius(m_Container, 0, 0);
    lv_obj_set_style_pad_all(m_Container, 2, 0);

    // Value Label
    m_ValueLabel = lv_label_create(m_Container);
    lv_obj_set_width( m_ValueLabel, LV_SIZE_CONTENT);  
    lv_obj_set_height( m_ValueLabel, LV_SIZE_CONTENT);  
    lv_obj_set_x( m_ValueLabel, -40 );
    lv_obj_set_y( m_ValueLabel, -2 );
    lv_obj_set_align( m_ValueLabel, LV_ALIGN_CENTER );
    lv_label_set_text(m_ValueLabel,"000");
    lv_obj_set_style_text_color(m_ValueLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT );
    lv_obj_set_style_text_opa(m_ValueLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(m_ValueLabel, &lv_font_seg7, LV_PART_MAIN| LV_STATE_DEFAULT);

    // Parameter Group (i.e. "EQ", "Phaser")
    m_ParamGroupLabel = lv_label_create(m_Container);
    lv_obj_set_width( m_ParamGroupLabel, LV_SIZE_CONTENT); 
    lv_obj_set_height( m_ParamGroupLabel, LV_SIZE_CONTENT); 
    lv_obj_set_x( m_ParamGroupLabel, 50 );
    lv_obj_set_y( m_ParamGroupLabel, 8 );
    lv_label_set_text(m_ParamGroupLabel,"EQ");
    lv_obj_set_style_text_color(m_ParamGroupLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT );
    lv_obj_set_style_text_opa(m_ParamGroupLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(m_ParamGroupLabel, &lv_font_unscii_8, LV_PART_MAIN| LV_STATE_DEFAULT);

    // Parameter Name
    m_ParamLabel = lv_label_create(m_Container);
    lv_obj_set_width( m_ParamLabel, LV_SIZE_CONTENT); 
    lv_obj_set_height( m_ParamLabel, LV_SIZE_CONTENT);
    lv_obj_set_x( m_ParamLabel, 50 );
    lv_obj_set_y( m_ParamLabel, 20 );
    lv_label_set_text(m_ParamLabel,"DRIVE");
    lv_obj_set_style_text_color(m_ParamLabel, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT );
    lv_obj_set_style_text_opa(m_ParamLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(m_ParamLabel, &lv_font_unscii_8, LV_PART_MAIN| LV_STATE_DEFAULT);

    // Progress bar at bottom (0-100%)
    m_Bar = lv_bar_create(m_Container);
    lv_obj_set_size(m_Bar, lv_pct(100), 8); // 8 pixel height bar
    lv_obj_set_align(m_Bar, LV_ALIGN_BOTTOM_MID);
    
    // Bar styling for monochrome display
    lv_obj_set_style_bg_color(m_Bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(m_Bar, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(m_Bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(m_Bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(m_Bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(m_Bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(m_Bar, 0, LV_PART_MAIN);
    
    // Set initial range
    lv_bar_set_range(m_Bar, 0, 100);
    lv_bar_set_value(m_Bar, 0, LV_ANIM_OFF);
    
    // The container itself must be focusable so the encoder (LVGL group)
    // delivers LV_EVENT_KEY events to us.
    lv_obj_add_flag(m_Container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m_Container, LV_OBJ_FLAG_CLICK_FOCUSABLE);

    // Event handler for encoder input
    lv_obj_add_event_cb(m_Container, param_event_cb, LV_EVENT_KEY, this);

    // Add to focus group and explicitly focus — there is no other
    // focusable widget on the parameter screen.
    lv_group_t* g = lv_group_get_default();
    if (g) {
        lv_group_add_obj(g, m_Container);
        lv_group_focus_obj(m_Container);
    }
}

void MonoParamControl::SetLabel(const std::string& label)
{
    if (m_ParamLabel)
        lv_label_set_text(m_ParamLabel, label.c_str());
}

void MonoParamControl::SetGroup(const std::string& group)
{
    if (m_ParamGroupLabel)
        lv_label_set_text(m_ParamGroupLabel, group.c_str());
}

void MonoParamControl::SetValue(int value)
{
    // Clamp value to range
    if (value < m_MinValue) value = m_MinValue;
    if (value > m_MaxValue) value = m_MaxValue;

    m_CurrentValue = value;
    UpdateValueDisplay();

    // NOTE: We deliberately do NOT fire OnValueChanged from here.
    // SetValue() is used in two ways:
    //   1. The screen pushes the current parameter value while the
    //      user scrolls through the list (browse mode) — must stay
    //      silent or we'd write the value back to the synth.
    //   2. param_event_cb() updates the value in edit mode and fires
    //      OnValueChanged itself afterwards.
}

void MonoParamControl::UpdateValueDisplay()
{
    int range = m_MaxValue - m_MinValue;
    int percentage = 0;

    if (range > 0)
        percentage = ((m_CurrentValue - m_MinValue) * 100) / range;
    else
        percentage = 100;             // min == max edge case

    if (m_Bar)
        lv_bar_set_value(m_Bar, percentage, LV_ANIM_OFF);

    if (m_ValueLabel)
        lv_label_set_text_fmt(m_ValueLabel, "%.3d", percentage);
}

void MonoParamControl::SetEditMode(bool editing)
{
    if (m_Editing == editing)
        return;

    m_Editing = editing;

    // Visual cue: in edit mode, draw a 1-pixel border around the value
    // label so it's obvious the encoder now changes the value.  The
    // header text ("EDIT PARAM" vs "BROWSE PARAM") is the primary
    // indicator; this is just a secondary on-glance hint.
    if (m_ValueLabel)
    {
        if (editing)
        {
            lv_obj_set_style_border_color(m_ValueLabel, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_border_opa  (m_ValueLabel, 255,              LV_PART_MAIN);
            lv_obj_set_style_border_width(m_ValueLabel, 1,                LV_PART_MAIN);
            lv_obj_set_style_pad_all     (m_ValueLabel, 1,                LV_PART_MAIN);
        }
        else
        {
            lv_obj_set_style_border_width(m_ValueLabel, 0,                LV_PART_MAIN);
            lv_obj_set_style_pad_all     (m_ValueLabel, 0,                LV_PART_MAIN);
        }
    }
}

void MonoParamControl::SetRange(int min, int max, int step)
{
    m_MinValue = min;
    m_MaxValue = max;
    
    // Validate step: must be positive and non-zero
    if (step <= 0) {
        step = 1; // Default to 1 if invalid
    }
    m_Step = step;
    
    // Update current value display
    SetValue(m_CurrentValue);
}

void MonoParamControl::SetEncoderEnabled(bool enabled)
{
    if (!m_Container)
        return;
    
    lv_group_t* g = lv_group_get_default();
    if (!g)
        return;
    
    if (enabled)
    {
        // Add to group if not already there
        if (!lv_obj_has_flag(m_Container, LV_OBJ_FLAG_CLICK_FOCUSABLE))
        {
            lv_obj_add_flag(m_Container, LV_OBJ_FLAG_CLICK_FOCUSABLE);
            lv_group_add_obj(g, m_Container);
            lv_group_focus_obj(m_Container);
        }
    }
    else
    {
        // Remove from group
        lv_obj_clear_flag(m_Container, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_group_remove_obj(m_Container);
    }
}

