// ui_widgets.h
#pragma once
#include <lvgl/lvgl.h>
#include <string>
#include <vector>
#include <functional>
#include "ui_types.h"

#define MY_SYMBOL_PRESET        "\xEE\xA4\x80" // 0xe900 music_cast
#define MY_SYMBOL_PRESETS       "\xEE\xA4\x81" // 0xe901 library_music
#define MY_SYMBOL_MENU          "\xEE\xA4\x82" // 0xe902 menu
#define MY_SYMBOL_SETTINGS      "\xEE\xA4\x83" // 0xe903 settings
#define MY_SYMBOL_INSTRUMENTS   "\xEE\xA4\x84" // 0xe904 sound_sampler
#define MY_SYMBOL_ABOUT         "\xEE\xA4\x85" // 0xe905 chat_info
#define MY_SYMBOL_DISPLAY       "\xEE\xA4\x86" // 0xe906 display_settings
#define MY_SYMBOL_SYSTEM        "\xEE\xA4\x87" // 0xe907 manufacturing
#define MY_SYMBOL_MIDI          "\xEE\xA4\x88" // 0xe908 settings_input_svideo
#define MY_SYMBOL_PARAMETER     "\xEE\xA4\x89" // 0xe909 rule_settings
#define MY_SYMBOL_EFFECTS       "\xEE\xA4\x8A" // 0xe90A filter_b_and_w
#define MY_SYMBOL_VOLUME        "\xEE\xA4\x8B" // 0xe90B volume_up
#define MY_SYMBOL_INSTRUMENT    "\xEE\xA4\x8C" // 0xe90C piano

struct IconListItem {
    std::string text;        // Hauptzeile
    std::string subtext;     // Zweite Zeile (optional)
    const char* icon;        // UTF‑8 Icon oder nullptr
    int id = -1;             // Optional ID (z.B. Preset-Index)
};

class IHeader {
public:
    virtual ~IHeader() = default;
    virtual void AttachTo(lv_obj_t* parent) = 0;
    virtual void SetTitle(const std::string& title) = 0;
};

class IListView {
public:
    virtual ~IListView() = default;
    virtual void AttachTo(lv_obj_t* parent) = 0;
    virtual void SetItems(const std::vector<IconListItem>& items) = 0;
    virtual void SetSelectedIndex(int index) = 0;
    virtual int  GetSelectedIndex() const = 0;
    std::function<void(int)> OnItemSelected;
};

class IParamControl {
public:
    virtual ~IParamControl() = default;
    virtual void AttachTo(lv_obj_t* parent) = 0;

    // Parameter name (e.g. "Drive", "Bass", "Attack")
    virtual void SetLabel(const std::string& label) = 0;

    // Parameter group (e.g. "EQ", "Phaser", "Piano").  Default no-op
    // for legacy implementations that don't render a group label.
    virtual void SetGroup(const std::string& /*group*/) {}

    // Set the displayed value (0..100 by default).  Purely visual —
    // does NOT trigger OnValueChanged (only user-driven encoder
    // turns do that).
    virtual void SetValue(int value) = 0;

    // Switch the widget between BROWSE and EDIT mode.  In browse mode
    // encoder rotation fires OnScroll(±1).  In edit mode encoder
    // rotation modifies the value and fires OnValueChanged().
    virtual void SetEditMode(bool /*editing*/) {}

    virtual void SetEncoderEnabled(bool enabled) = 0;  // Enable/disable encoder input

    // Fired when the user changed the value via the encoder (edit mode).
    std::function<void(int)> OnValueChanged;

    // Fired in browse mode on each encoder tick.  delta is +1 / -1.
    std::function<void(int)> OnScroll;

    // Fired on encoder click (short press) — typically used to toggle
    // between browse and edit mode.
    std::function<void()> OnEnter;
};
