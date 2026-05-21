// ui_types.h
#pragma once

enum class DisplayType {
    Mono128x64,
    Mono128x32,
    Color320x240,
    HDMI
};

enum class UIEvent {
    EncoderCW,
    EncoderCCW,
    EncoderClick,
    EncoderBack,

    SelectItem,      // z.B. in Listen
    ChangeParam,     // z.B. Drehregler
    NavigateToHome,
    NavigateToMainMenu,
    NavigateToPresetBrowser,
    NavigateBack,
};
