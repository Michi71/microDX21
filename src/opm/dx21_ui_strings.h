// ============================================================================
// dx21_ui_strings.h
//
// All UI strings extracted from the original Yamaha DX21 ROM V1.5 firmware
// (file: src/opm/firmware/dx21_rom_v1_5.asm, reverse-engineered assembler
// representation).
//
// Sources in ROM:
//   dat_F610 (Z. 13734) - EDIT-mode parameter names (algorithm, feedback, LFO, ...)
//   dat_FBDA (Z. 14440) - FUNCTION-mode menu items
//   dat_F03E (Z. 12983) - PLAY-mode / MEMORY STORE labels
//   dat_F184 (Z. 13149) - Performance-mode store / PMEM labels
//   dat_FB3A (Z. 14356) - Note names (transpose display)
//   dat_F347 (Z. 13382) - ON / OFF toggle
//   dat_EB65 (Z. 12020) - Cartridge / Tape dialogs
//   dat_E6E6 (Z. 11275) - MIDI status messages
//   dat_E9B7 (Z. 11781) - MIDI error messages
//   str_Buff (Z. 12873) - "Buff" buffer indicator
//
// All strings are stored as 16-char zero-padded char arrays so the
// microDX21 UI can copy them directly into a 128x32 framebuffer via memcpy.
// Maximum width fits the 128/6=21-char grid, the original HD44780 used 16.
// ============================================================================

#ifndef DX21_UI_STRINGS_H
#define DX21_UI_STRINGS_H

#include <circle/types.h>

namespace DX21UI {

// Mode titles (line 1 of the LCD, 16 chars max, original = 16 chars).
// Matches rtn_85 dispatch in the ROM: cases 1..6.
constexpr char MODE_TITLE_PLAY[]        = "PLAY";
constexpr char MODE_TITLE_EDIT[]        = "EDIT";
constexpr char MODE_TITLE_PERFORMANCE[] = "PERFORM";
constexpr char MODE_TITLE_FUNCTION[]    = "FUNCTION";
constexpr char MODE_TITLE_MEMORY[]      = "MEMORY";
constexpr char MODE_TITLE_COMPARE[]     = "COMPARE";

// EDIT-mode parameter labels (from dat_F610).
// Indexed by parameter enum (see EditParam below).
// All entries 16 chars, right-padded with spaces.
constexpr const char* EDIT_PARAM_NAMES[] = {
    "ALG             ",  //  0  Algorithm
    "ALGORITHM SELECT",  //  1  Algorithm select (long form)
    "FEEDBACK        ",  //  2  Feedback
    "LFO WAVE:       ",  //  3  LFO waveform
    "LFO SPEED       ",  //  4  LFO speed
    "LFO DELAY       ",  //  5  LFO delay
    "LFO PMD         ",  //  6  LFO pitch mod depth
    "LFO AMD         ",  //  7  LFO amp mod depth
    "P MOD SENS.     ",  //  8  Pitch mod sensitivity
    "A MOD SENS.     ",  //  9  Amp mod sensitivity
    "E BIAS SENS.    ",  // 10  EG bias sensitivity
    "KEY VELOCITY    ",  // 11  Key velocity
    "FREQUENCY =     ",  // 12  Frequency (coarse)
    "DETUNE    =     ",  // 13  Detune
    " EG  AR         ",  // 14  EG attack rate (op)
    " EG  D1R        ",  // 15  EG decay 1 rate
    " EG  D1L        ",  // 16  EG decay 1 level
    " EG  D2R        ",  // 17  EG decay 2 rate
    " EG  RR         ",  // 18  EG release rate
    "OUTPUT LEVEL    ",  // 19  Output level (op)
    "RATE SCALE      ",  // 20  Rate scaling
    "LEVEL SCALE     ",  // 21  Level scaling
    "PEG RATE 1      ",  // 22  Pitch EG rate 1
    "PEG LEVEL 1     ",  // 23  Pitch EG level 1
    "PEG RATE 2      ",  // 24  Pitch EG rate 2
    "PEG LEVEL 2     ",  // 25  Pitch EG level 2
    "PEG RATE 3      ",  // 26  Pitch EG rate 3
    "PEG LEVEL 3     ",  // 27  Pitch EG level 3
    "SAW UP          ",  // 28  LFO waveform alt
    "SQUARE          ",  // 29
    "TRIANGL         ",  // 30
    "S/HOLD          ",  // 31
    "EG Copy         ",  // 32  EG copy utility
    "from OP         ",  // 33  EG copy from op
    "LFO SYNC :      ",  // 34  LFO key sync
    "Memory Protected",  // 35  Memory protection
};
constexpr int EDIT_PARAM_COUNT = sizeof(EDIT_PARAM_NAMES) / sizeof(EDIT_PARAM_NAMES[0]);

// LFO waveform names (dat_F610 trailing bytes)
constexpr const char* LFO_WAVE_NAMES[] = {
    "TRIANGLE", "SAW UP", "SQUARE", "S/HOLD"
};
constexpr int LFO_WAVE_COUNT = 4;

// FUNCTION-mode menu items (from dat_FBDA).
constexpr const char* FUNCTION_NAMES[] = {
    "FUNCTION CONTROL",  //  0  Top-level header
    "Master Tune =   ",  //  1
    "Dual Detune     ",  //  2
    "Midi Switch :   ",  //  3
    "Midi Ch Info:   ",  //  4
    "Midi Sy Info:   ",  //  5
    "Midi Recv Ch =  ",  //  6
    "Midi Omni : ON  ",  //  7
    "Recall Edit ?   ",  //  8
    "Are You Sure ?  ",  //  9
    "Init. Voice ?   ",  // 10
    "Save to Tape ?  ",  // 11
    "from Mem to Tape",  // 12
    "Verify    Tape ?",  // 13
    "Verify Tape     ",  // 14
    "Load from Tape ?",  // 15
    "from Tape to Mem",  // 16
    "Load  Single ?  ",  // 17
    "Tape # ? to Buff",  // 18
    "Group to Bank ? ",  // 19
    "Group (1-16) ?  ",  // 20
    "Bank (1-4) ?    ",  // 21
    "Voice to Buff  ?",  // 22
    "Mem Protect:    ",  // 23
    "Poly Mode       ",  // 24
    "Mono Mode       ",  // 25
    "P Bend Range    ",  // 26
    "Fingered Porta  ",  // 27
    "Full Time Porta ",  // 28
    "Porta Time      ",  // 29
    "Foot Volume     ",  // 30
    "Foot Sustain:   ",  // 31
    "Foot Porta  :   ",  // 32
    "MW Pitch        ",  // 33
    "MW Amplitude    ",  // 34
    "BC Pitch        ",  // 35
    "BC Amplitude    ",  // 36
    "BC Pitch Bias   ",  // 37
    "BC EG Bias      ",  // 38
    "Middle C =      ",  // 39
    "Midi Trns Ch =  ",  // 40
    "Midi Transmit ? ",  // 41
    "Chorus      :   ",  // 42
    "Bend Mode =     ",  // 43
    "Key Shift =     ",  // 44
    "Name :          ",  // 45
};
constexpr int FUNCTION_COUNT = sizeof(FUNCTION_NAMES) / sizeof(FUNCTION_NAMES[0]);

// PLAY/PERFORMANCE mode labels (from dat_F03E and dat_F184).
constexpr const char* PLAY_LABELS[] = {
    "PLAY SINGLE     ",   // 0 Single-voice play
    "PLAY DUAL       ",   // 1 Dual-voice play
    "PLAY SPLIT      ",   // 2 Split-keyboard play
    "MEMORY STORE    ",   // 3 Memory store dialog
    "Performance Mode",   // 4 Performance store header
    "PMEM ?          ",   // 5 Performance memory select
};
constexpr int PLAY_LABEL_COUNT = 6;

// ON / OFF toggle (from dat_F347).
constexpr const char* ON_OFF[]  = { "OFF", "ON" };

// Note name table (from dat_FB3A, 12 semitones + space padding).
// Used for transpose / master tune display in MIDI menu.
constexpr const char* NOTE_NAMES[] = {
    "C ", "C#", "D ", "D#", "E ", "F ",
    "F#", "G ", "G#", "A ", "A#", "B "
};
constexpr int NOTE_NAMES_COUNT = 12;

// Cartridge / tape dialogs (from dat_EB65).
// Top-level: Save / Load / Verify actions in MEMORY mode (0..2).
// Confirmation: YES/NO dialog (3..4).
// Group select: "GROUP (1-16) ?" (5..20, 16 entries; the original DX21
// shows a 2-digit group number in the big-7seg area, not a label).
// Result feedback: ERR / Completed (21..22).
constexpr const char* TAPE_LABELS[] = {
    // 0..2: top-level actions (page 0 of MEMORY mode)
    "Save to Tape  ?",   //  0  Save 32 RAM voices to SD bank
    "Load from Tape?",   //  1  Load 32 RAM voices from SD bank
    "Verify    Tape?",   //  2  Verify SD bank matches RAM
    // 3..4: YES/NO confirmation
    "       ?  NO ",     //  3  NO selected (default)
    "       ? YES ",     //  4  YES selected
    // 5..20: Group select 1..16 (label is "GROUP (N) ?", page 1 of MEMORY)
    "GROUP ( 1) ?  ",    //  5
    "GROUP ( 2) ?  ",    //  6
    "GROUP ( 3) ?  ",    //  7
    "GROUP ( 4) ?  ",    //  8
    "GROUP ( 5) ?  ",    //  9
    "GROUP ( 6) ?  ",    // 10
    "GROUP ( 7) ?  ",    // 11
    "GROUP ( 8) ?  ",    // 12
    "GROUP ( 9) ?  ",    // 13
    "GROUP (10) ?  ",    // 14
    "GROUP (11) ?  ",    // 15
    "GROUP (12) ?  ",    // 16
    "GROUP (13) ?  ",    // 17
    "GROUP (14) ?  ",    // 18
    "GROUP (15) ?  ",    // 19
    "GROUP (16) ?  ",    // 20
    // 21..22: result feedback
    "ERR             ",  // 21
    "Completed       ",  // 22
};
constexpr int TAPE_LABEL_COUNT = sizeof(TAPE_LABELS) / sizeof(TAPE_LABELS[0]);
constexpr int TAPE_GROUP_FIRST  = 5;    // first group label index
constexpr int TAPE_GROUP_LAST   = 20;   // last group label index
constexpr int TAPE_NUM_GROUPS   = TAPE_GROUP_LAST - TAPE_GROUP_FIRST + 1;
constexpr int TAPE_RESULT_ERR   = 21;
constexpr int TAPE_RESULT_OK    = 22;

// MIDI status messages (from dat_E6E6 / dat_E9B7).
constexpr const char* MIDI_STATUS[] = {
    "MIDI            ",
    " MIDI RECEIVED  ",
    "MIDI CSUM ERROR ",
    "MIDI BUFFER FULL",
    "MIDI DATA ERROR ",
};
constexpr int MIDI_STATUS_COUNT = sizeof(MIDI_STATUS) / sizeof(MIDI_STATUS[0]);

// Bank labels (A1-A8, A9-A16, B1-B8, B9-B16, from dat_FBDA tail).
constexpr const char* BANK_LABELS[] = {
    "A1-A8     ",
    "A9-A16    ",
    "B1-A8     ",
    "B9-B16    ",
};
constexpr int BANK_LABEL_COUNT = 4;

// Display modes - direct mapping to rtn_85 / rtn_184 switch cases in ROM
// (matches the internal $1C65 mode byte).
enum Mode : u8 {
    kModePlay        = 1,    // rtn_85 case 1 -> lbl_EEB4
    kModeEdit        = 2,    // rtn_85 case 2 -> lbl_EEF6
    kModePerformance = 4,    // rtn_85 case 4 -> lbl_EF38
    kModeFunction    = 5,    // rtn_85 case 5 -> lbl_EEAE
    kModeMemory      = 6,    // rtn_85 case 6 -> lbl_EEB1
};

// Sub-flags for compare overlay.
enum Flag : u8 {
    kFlagCompare     = 0x10,  // Voice = EDIT (compare with buffer)
    kFlagMemoryProt  = 0x20,  // Memory protected (dat_F610:35)
};

} // namespace DX21UI

#endif // DX21_UI_STRINGS_H
