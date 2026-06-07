//
// display_dx21.cpp
//
// CDX21Display implementation for 128x32 SSD1305 SPI OLED.
// Renders the 6 DX21 modes (PLAY/EDIT/PERFORMANCE/FUNCTION/MEMORY)
// using the original ROM's display layout. Once a COPMEmuAdapter
// is bound via SetAdapter(), all values shown on the display come
// live from the synth.
//

#include "display_dx21.h"
#include "audio/opmemuadapter.h"
#include "opm/opmemu.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <string.h>
#include <stdio.h>

using namespace DX21UI;
using namespace DX21UI7Seg;

// External 6x8 font (from Circle's addon/display/font6x8.h).
// Each char = 6 cols x 8 rows, stored as 8 bytes per char (1 bit per row).
#include <font6x8.h>
constexpr int FONT6x8_W = 6;
constexpr int FONT6x8_H = 8;

static const char FromDisplay[] = "dx21disp";

// ────────────────────────────────────────────────────────────────────
// EDIT-mode parameter mapping
// ────────────────────────────────────────────────────────────────────
// EDIT_PARAM_NAMES has 36 entries (in display order), kParamXxx has 36
// entries (in functional-group order). They're not 1:1, and several
// EDIT params (PEG rates, level scaling, key velocity, freq/detune)
// don't have a getter in COPMEmuAdapter at all. We map what we can
// and fall back to "n/a" for the rest.
//
// Map: EDIT_PARAM_NAMES index -> DX21ParamIndex (or -1 if unavailable)
// ────────────────────────────────────────────────────────────────────
static const int kEditToAdapter[] = {
    /*  0  ALG                */  kParamAlgorithm,  // 12
    /*  1  ALGORITHM SELECT   */  kParamAlgorithm,  // 12 (long form)
    /*  2  FEEDBACK           */  kParamFeedback,    // 13
    /*  3  LFO WAVE:          */  kParamLFOWave,      // 15
    /*  4  LFO SPEED          */  kParamLFOSpeed,     // 8
    /*  5  LFO DELAY          */  kParamLFODelay,     // 9
    /*  6  LFO PMD            */  kParamPMD,          // 10
    /*  7  LFO AMD            */  kParamAMD,          // 11
    /*  8  P MOD SENS.        */  -1,                 // (no getter)
    /*  9  A MOD SENS.        */  -1,
    /* 10  E BIAS SENS.       */  -1,
    /* 11  KEY VELOCITY       */  -1,
    /* 12  FREQUENCY =        */  -1,
    /* 13  DETUNE    =        */  -1,
    /* 14  EG  AR (OP1)       */  kParamOp0AR,        // 16
    /* 15  EG  D1R            */  kParamOp0D1R,
    /* 16  EG  D1L            */  kParamOp0D1L,
    /* 17  EG  D2R            */  -1,
    /* 18  EG  RR             */  -1,
    /* 19  OUTPUT LEVEL       */  kParamOp0Out,       // 19
    /* 20  RATE SCALE         */  -1,
    /* 21  LEVEL SCALE        */  -1,
    /* 22  PEG RATE 1         */  -1,
    /* 23  PEG LEVEL 1        */  -1,
    /* 24  PEG RATE 2         */  -1,
    /* 25  PEG LEVEL 2        */  -1,
    /* 26  PEG RATE 3         */  -1,
    /* 27  PEG LEVEL 3        */  -1,
    /* 28  SAW UP             */  kParamLFOWave,      // 15 (sets wave=0)
    /* 29  SQUARE             */  kParamLFOWave,      // 15 (sets wave=1)
    /* 30  TRIANGL            */  kParamLFOWave,      // 15 (sets wave=2)
    /* 31  S/HOLD             */  kParamLFOWave,      // 15 (sets wave=3)
    /* 32  EG Copy            */  -1,                 // utility, not a param
    /* 33  from OP            */  -1,
    /* 34  LFO SYNC :         */  kParamLFOSync,      // 14
    /* 35  Memory Protected   */  -1,                 // global flag
};
static_assert(sizeof(kEditToAdapter)/sizeof(kEditToAdapter[0]) == EDIT_PARAM_COUNT,
              "kEditToAdapter must match EDIT_PARAM_COUNT");

// Same for OP-cycles 1-3 (OP2/OP3/OP4). We only map OP1 above because
// the EDIT_PARAM_NAMES list shows OP1 (carrier 1) EG params; cycling
// through the OP block would require duplicating the 4 OP blocks in
// kParamXxx (we'd index kParamOp1AR..kParamOp3Out for the others).
// For now, only OP1 is live; the other ops are tagged "n/a".

// ────────────────────────────────────────────────────────────────────
// FUNCTION-mode parameter mapping (46 items, in display order)
// ────────────────────────────────────────────────────────────────────
// FUNCTION_NAMES shows 46 items. We map each to either a kParamXxx
// getter (returns a 0..1 float → display as "=NN%") or a hardcoded
// pair (ON/OFF / specific numeric range).
//
// -1 means: not yet wired to the synth; show as "—" in the value row.
// ────────────────────────────────────────────────────────────────────
static const int kFunctionToAdapter[] = {
    /*  0  FUNCTION CONTROL   */  -1,    // header
    /*  1  Master Tune =      */  -1,    // no getter (DX21 master tune is int -64..+63)
    /*  2  Dual Detune        */  -1,
    /*  3  Midi Switch :      */  -1,    // could be ON/OFF when SysEx is bound
    /*  4  Midi Ch Info:      */  -1,    // info display
    /*  5  Midi Sy Info:      */  -1,
    /*  6  Midi Recv Ch =     */  -1,
    /*  7  Midi Omni : ON     */  -1,    // ON/OFF
    /*  8  Recall Edit ?      */  -1,    // dialog
    /*  9  Are You Sure ?     */  -1,
    /* 10  Init. Voice ?      */  -1,
    /* 11  Save to Tape ?     */  -1,
    /* 12  from Mem to Tape   */  -1,
    /* 13  Verify    Tape ?   */  -1,
    /* 14  Verify Tape        */  -1,
    /* 15  Load from Tape ?   */  -1,
    /* 16  from Tape to Mem   */  -1,
    /* 17  Load  Single ?     */  -1,
    /* 18  Tape # ? to Buff   */  -1,
    /* 19  Group to Bank ?    */  -1,
    /* 20  Group (1-16) ?     */  -1,
    /* 21  Bank (1-4) ?       */  -1,
    /* 22  Voice to Buff  ?   */  -1,
    /* 23  Mem Protect:       */  -1,    // boolean flag (separate state)
    /* 24  Poly Mode          */  kParamPlayMode,    // Single/Dual/Split (3)
    /* 25  Mono Mode          */  -1,    // boolean (single voice)
    /* 26  P Bend Range       */  kParamPitchBendRange,  // 0..12
    /* 27  Fingered Porta     */  -1,
    /* 28  Full Time Porta    */  kParamPortamentoMode,  // Off/FullTime/Fingered
    /* 29  Porta Time         */  kParamPortamentoRate,  // 0..99
    /* 30  Foot Volume        */  -1,
    /* 31  Foot Sustain:      */  -1,
    /* 32  Foot Porta  :      */  -1,
    /* 33  MW Pitch           */  -1,
    /* 34  MW Amplitude       */  -1,
    /* 35  BC Pitch           */  -1,
    /* 36  BC Amplitude       */  -1,
    /* 37  BC Pitch Bias      */  -1,
    /* 38  BC EG Bias         */  -1,
    /* 39  Middle C =         */  -1,
    /* 40  Midi Trns Ch =     */  -1,    // no adapter getter
    /* 41  Midi Transmit ?    */  -1,
    /* 42  Chorus      :      */  kParamEnsemble,    // 0/1
    /* 43  Bend Mode =        */  -1,
    /* 44  Key Shift =        */  -1,
    /* 45  Name :             */  -1,
};
static_assert(sizeof(kFunctionToAdapter)/sizeof(kFunctionToAdapter[0]) == FUNCTION_COUNT,
              "kFunctionToAdapter must match FUNCTION_COUNT");

// ────────────────────────────────────────────────────────────────────
// Bank labels (4 banks of 16 voices each, 0..15 in each bank)
// ────────────────────────────────────────────────────────────────────
static const char* kBankLabel(int voiceNum /* 0-based */) {
    if (voiceNum < 0) return "A1-A8";
    int bank = voiceNum / 16;     // 0..7 (we have 128 voices, 8 banks)
    if (bank < 4) {
        return (bank & 1) == 0 ? "A1-A8" : "A9-A16";
    } else {
        return (bank & 1) == 0 ? "B1-A8" : "B9-B16";
    }
}

// ════════════════════════════════════════════════════════════════════
// Construction / lifecycle
// ════════════════════════════════════════════════════════════════════

CDX21Display::CDX21Display(CSPIMaster* pSPI, const Config& cfg)
    : m_pSPI(pSPI)
    , m_Config(cfg)
    , m_pDisplay(nullptr)
    , m_Mode(kModePlay)
    , m_ParamIdx(0)
    , m_Value(0)
    , m_ValueStr(nullptr)
    , m_VoiceName("Init Voice")
    , m_VoiceNum(1)
    , m_Status(nullptr)
    , m_bCompare(false)
    , m_bMemProt(false)
    , m_bDirty(true)
    , m_pAdapter(nullptr)
    , m_LastRenderMs(0)
{
    memset(m_PageBuf, 0, sizeof(m_PageBuf));
}

CDX21Display::~CDX21Display() {
    delete m_pDisplay;
}

void CDX21Display::SetAdapter(COPMEmuAdapter* pAdapter) {
    m_pAdapter = pAdapter;
    MarkDirty();
}

bool CDX21Display::Initialize() {
    m_pDisplay = new CSSD1305SPIDisplay(m_pSPI,
                                         m_Config.nDCPin,
                                         m_Config.nResetPin,
                                         kWidth, kHeight,
                                         m_Config.nCPOL, m_Config.nCPHA,
                                         m_Config.nClockSpeed,
                                         m_Config.nChipSelect);
    if (!m_pDisplay) {
        CLogger::Get()->Write(FromDisplay, LogError, "alloc failed");
        return false;
    }
    if (!m_pDisplay->Initialize()) {
        CLogger::Get()->Write(FromDisplay, LogError, "init failed");
        delete m_pDisplay;
        m_pDisplay = nullptr;
        return false;
    }
    m_pDisplay->On();
    m_pDisplay->Clear();
    m_pDisplay->Show();
    m_bDirty = true;
    CLogger::Get()->Write(FromDisplay, LogNotice,
        "128x32 SSD1305 SPI display ready (DC=%u RST=%u CS=%u @%u Hz)",
        m_Config.nDCPin,
        m_Config.nResetPin == GPIO_PINS ? 0 : m_Config.nResetPin,
        m_Config.nChipSelect, m_Config.nClockSpeed);
    return true;
}

// ════════════════════════════════════════════════════════════════════
// Framebuffer primitives
// ════════════════════════════════════════════════════════════════════

void CDX21Display::ClearFrameBuffer() {
    if (m_pDisplay) m_pDisplay->Clear();
}

void CDX21Display::DrawHLine(int x, int y, int w) {
    if (!m_pDisplay) return;
    for (int dx = 0; dx < w; ++dx) {
        int xi = x + dx;
        if (xi < 0 || xi >= (int)kWidth || y < 0 || y >= (int)kHeight) continue;
        m_pDisplay->SetPixel(xi, y, 1);
    }
}

void CDX21Display::DrawVLine(int x, int y, int h) {
    if (!m_pDisplay) return;
    for (int dy = 0; dy < h; ++dy) {
        int yi = y + dy;
        if (x < 0 || x >= (int)kWidth || yi < 0 || yi >= (int)kHeight) continue;
        m_pDisplay->SetPixel(x, yi, 1);
    }
}

void CDX21Display::DrawText6x8(int x, int y, const char* s, int maxChars,
                                bool inverted) {
    if (!m_pDisplay || !s) return;
    for (int i = 0; i < maxChars && s[i] != '\0'; ++i) {
        char c = s[i];
        if (c < ' ' || c > '~') c = ' ';
        const u8* glyph = Font6x8[c - ' '];
        int gx = x + i * FONT6x8_W;
        for (int col = 0; col < FONT6x8_W; ++col) {
            u8 bits = glyph[col];
            if (inverted) bits = ~bits;
            for (int row = 0; row < FONT6x8_H; ++row) {
                if (bits & (1 << row)) {
                    int px = gx + col;
                    int py = y + row;
                    if (px < (int)kWidth && py < (int)kHeight) {
                        m_pDisplay->SetPixel(px, py, 1);
                    }
                }
            }
        }
    }
}

void CDX21Display::DrawText6x8_Page(int page, const char* s, bool inverted) {
    int y = page * FONT6x8_H;
    DrawText6x8(0, y, s, 21, inverted);
}

void CDX21Display::DrawCursor(int x, int y, int w, int h) {
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            int px = x + dx, py = y + dy;
            if (px < (int)kWidth && py < (int)kHeight) {
                m_pDisplay->SetPixel(px, py, 1);
            }
        }
    }
}

void CDX21Display::DrawGlyph7Seg(int gx, int gy, Glyph g, bool inverted) {
    if (!m_pDisplay) return;
    const u16* col_pat = kFont[g];
    for (int col = 0; col < CHAR_W; ++col) {
        u16 pat = col_pat[col];
        if (inverted) pat = ~pat;
        for (int row = 0; row < CHAR_H; ++row) {
            if (pat & (1u << row)) {
                int px = gx + col;
                int py = gy + row;
                if (px < (int)kWidth && py < (int)kHeight) {
                    m_pDisplay->SetPixel(px, py, 1);
                }
            }
        }
    }
}

void CDX21Display::DrawBigNumber(int x, int y, int value, int minDigits) {
    if (!m_pDisplay) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    int len = (int)strlen(buf);
    if (len < minDigits) {
        int pad = minDigits - len;
        int total = pad + len;
        if (x + total * CHAR_W > (int)kWidth) {
            int avail = ((int)kWidth - x) / CHAR_W;
            if (avail < 1) return;
            if (total > avail) {
                int drop = total - avail;
                if (drop > pad) drop = pad;
                pad -= drop;
                total -= drop;
            }
        }
        for (int i = 0; i < pad; ++i) {
            DrawGlyph7Seg(x, y, G_SPACE);
            x += CHAR_W;
        }
        for (int i = 0; i < len; ++i) {
            DrawGlyph7Seg(x, y, FromAscii(buf[i]));
            x += CHAR_W;
        }
    } else {
        int total = len;
        if (x + total * CHAR_W > (int)kWidth) {
            int avail = ((int)kWidth - x) / CHAR_W;
            if (avail < 1) return;
            if (total > avail) total = avail;
        }
        for (int i = 0; i < total; ++i) {
            DrawGlyph7Seg(x, y, FromAscii(buf[i]));
            x += CHAR_W;
        }
    }
}

void CDX21Display::DrawBigString(int x, int y, const char* s) {
    if (!m_pDisplay || !s) return;
    while (*s && x + CHAR_W <= (int)kWidth) {
        DrawGlyph7Seg(x, y, FromAscii(*s));
        x += CHAR_W;
        ++s;
    }
}

// ════════════════════════════════════════════════════════════════════
// Synth value helpers
// ════════════════════════════════════════════════════════════════════

// Read the current voice name and number from the adapter (or fall
// back to m_VoiceName / m_VoiceNum if no adapter is bound).
static void ReadVoiceInfo(COPMEmuAdapter* a, char nameOut[17], int& numOut) {
    if (a) {
        a->getInstrumentName(nameOut);
        numOut = a->getInstrument() + 1;  // 1-based
    } else {
        if (nameOut && nameOut[0] == '\0') {
            // not bound - use whatever SetVoiceName was given
        }
        numOut = numOut;  // keep m_VoiceNum
    }
}

// Read a 0..255 raw value for a given EDIT_PARAM_NAMES index.
// Returns -1 if the param has no live getter.
static int ReadEditValue(COPMEmuAdapter* a, int paramIdx) {
    if (!a) return -1;
    if (paramIdx < 0 || paramIdx >= EDIT_PARAM_COUNT) return -1;
    int kp = kEditToAdapter[paramIdx];
    if (kp < 0) return -1;
    float v = a->getParameter(kp);
    // Scale to 0..255. Each param has its own natural range; the
    // adapter's getParameter() already normalises to 0..1, so we
    // multiply by 255. The user sees 0..255 here, which matches the
    // EDIT-mode mental model of "raw VCED byte".
    int raw = (int)(v * 255.0f + 0.5f);
    if (raw < 0) raw = 0;
    if (raw > 255) raw = 255;
    return raw;
}

// Read a 0..255 raw value for a given FUNCTION_NAMES index. Returns
// -1 if no live binding. For ON/OFF booleans, returns 0 or 1.
static int ReadFunctionValue(COPMEmuAdapter* a, int funcIdx) {
    if (!a) return -1;
    if (funcIdx < 0 || funcIdx >= FUNCTION_COUNT) return -1;
    int kp = kFunctionToAdapter[funcIdx];
    if (kp < 0) return -1;
    float v = a->getParameter(kp);
    int raw = (int)(v * 255.0f + 0.5f);
    if (raw < 0) raw = 0;
    if (raw > 255) raw = 255;
    return raw;
}

// Read the DX21 play-mode as a 0/1/2 integer (Single/Dual/Split).
// Returns -1 if no adapter.
static int ReadPlayMode(COPMEmuAdapter* a) {
    if (!a) return -1;
    return (int)(a->getParameter(kParamPlayMode) * 2.0f + 0.5f);
}

static const char* kPlayModeNames[] = { "SINGLE", "DUAL", "SPLIT" };

// ════════════════════════════════════════════════════════════════════
// Per-mode render dispatch
// ════════════════════════════════════════════════════════════════════

void CDX21Display::RenderPlayMode() {
    char line[32];
    char nameBuf[17] = {0};

    int voiceNum = m_VoiceNum;
    if (m_pAdapter) {
        m_pAdapter->getInstrumentName(nameBuf);
        voiceNum = m_pAdapter->getInstrument() + 1;
    } else if (m_VoiceName) {
        strncpy(nameBuf, m_VoiceName, sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
    }

    int playMode = ReadPlayMode(m_pAdapter);
    const char* playName = (playMode >= 0 && playMode < 3) ? kPlayModeNames[playMode] : "PLAY";

    // Page 0: "<NNN>" with the current voice number
    snprintf(line, sizeof(line), "  <%3d>          ", voiceNum);
    DrawText6x8(0, 0, line, 16);

    // Page 1: voice name (inverted if COMPARE active)
    char big[17];
    snprintf(big, sizeof(big), "%-16.16s", nameBuf);
    DrawText6x8(0, 8, big, 16, m_bCompare);

    // Page 2: bank group (A1-A8, A9-A16, B1-A8, B9-B16) - derived from voice number
    int bankIdx = (voiceNum - 1) / 16;
    const char* bankLabel = (bankIdx < 4) ? BANK_LABELS[bankIdx] : "B9-B16";
    DrawText6x8(0, 16, bankLabel, 16);

    // Page 3: PLAY mode name, or status if set
    if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
    } else {
        char modeLine[17];
        snprintf(modeLine, sizeof(modeLine), "PLAY %-10s", playName);
        DrawText6x8(0, 24, modeLine, 16);
    }
}

void CDX21Display::RenderEditMode() {
    char line[32];

    // Page 0: "EDIT  N/36"
    snprintf(line, sizeof(line), "%-9s %2d/%-2d     ",
             MODE_TITLE_EDIT, m_ParamIdx + 1, EDIT_PARAM_COUNT);
    DrawText6x8(0, 0, line, 16);

    // Page 1: big 3-digit value (read from adapter, or fallback to m_Value)
    int value = ReadEditValue(m_pAdapter, m_ParamIdx);
    if (value < 0) value = m_Value;
    DrawBigNumber(0, 8, value, 3);

    // Page 2: parameter name
    const char* pname = (m_ParamIdx >= 0 && m_ParamIdx < EDIT_PARAM_COUNT)
        ? EDIT_PARAM_NAMES[m_ParamIdx]
        : "                 ";
    DrawText6x8(0, 16, pname, 16);

    // Page 3: status / override string / blank
    if (m_ValueStr) {
        snprintf(line, sizeof(line), "%-16.16s", m_ValueStr);
    } else if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
    } else {
        // Show the adapter-side display string if available.
        if (m_pAdapter) {
            std::string s = m_pAdapter->getParameterDisplay(
                kEditToAdapter[m_ParamIdx >= 0 && m_ParamIdx < EDIT_PARAM_COUNT
                                ? m_ParamIdx : 0]);
            snprintf(line, sizeof(line), "%-16.16s", s.c_str());
        } else {
            line[0] = '\0';
        }
    }
    DrawText6x8(0, 24, line, 16);
}

void CDX21Display::RenderPerformanceMode() {
    char line[32];
    char nameBuf[17] = {0};

    if (m_pAdapter) {
        m_pAdapter->getInstrumentName(nameBuf);
    } else if (m_VoiceName) {
        strncpy(nameBuf, m_VoiceName, sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
    }

    // Page 0: "PERFORM  NNN"
    snprintf(line, sizeof(line), "%-9s %3d      ",
             MODE_TITLE_PERFORMANCE, m_VoiceNum);
    DrawText6x8(0, 0, line, 16);

    // Page 1: voice name (or "BUFF" when COMPARE is active)
    if (m_bCompare) {
        DrawBigString(0, 8, "BUFF");
    } else {
        DrawBigString(0, 8, nameBuf);
    }

    // Page 2: sub-label
    const char* pname = (m_ParamIdx >= 0 && m_ParamIdx < PLAY_LABEL_COUNT)
        ? PLAY_LABELS[m_ParamIdx]
        : "                ";
    DrawText6x8(0, 16, pname, 16);

    // Page 3: bank / status
    if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
    } else {
        int bankIdx = (m_VoiceNum - 1) / 16;
        const char* bankLabel = (bankIdx < 4) ? BANK_LABELS[bankIdx] : "B9-B16";
        DrawText6x8(0, 24, bankLabel, 16);
    }
}

void CDX21Display::RenderFunctionMode() {
    char line[32];

    // Page 0: "FUNCTION  N/46"
    snprintf(line, sizeof(line), "%-9s %2d/%-2d    ",
             MODE_TITLE_FUNCTION, m_ParamIdx + 1, FUNCTION_COUNT);
    DrawText6x8(0, 0, line, 16);

    // Page 1: function name in big
    const char* fname = (m_ParamIdx >= 0 && m_ParamIdx < FUNCTION_COUNT)
        ? FUNCTION_NAMES[m_ParamIdx]
        : "                ";
    DrawBigString(0, 8, fname);

    // Page 2: value (numeric, ON/OFF, or "n/a")
    int value = ReadFunctionValue(m_pAdapter, m_ParamIdx);
    if (m_ValueStr) {
        snprintf(line, sizeof(line), "%-16.16s", m_ValueStr);
    } else if (value >= 0) {
        // Show as =NN with a 0..255 raw byte value.
        snprintf(line, sizeof(line), "=%-15d", value);
    } else {
        snprintf(line, sizeof(line), "=n/a           ");
    }
    DrawText6x8(0, 16, line, 16);

    // Page 3: status
    if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
    } else {
        line[0] = '\0';
    }
    DrawText6x8(0, 24, line, 16);
}

void CDX21Display::RenderMemoryMode() {
    char line[32];

    // Page 0: "MEMORY  N"
    snprintf(line, sizeof(line), "%-9s %2d      ",
             MODE_TITLE_MEMORY, m_ParamIdx);
    DrawText6x8(0, 0, line, 16);

    // Page 1: bank name in big
    const char* bankText = m_ValueStr ? m_ValueStr : "TAPE";
    DrawBigString(0, 8, bankText);

    // Page 2: tape dialog label
    const char* tname = (m_ParamIdx >= 0 && m_ParamIdx < TAPE_LABEL_COUNT)
        ? TAPE_LABELS[m_ParamIdx]
        : "                ";
    DrawText6x8(0, 16, tname, 16);

    // Page 3: status
    if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
    } else {
        line[0] = '\0';
    }
    DrawText6x8(0, 24, line, 16);
}

void CDX21Display::InvalidateIfStale(unsigned maxAgeMs) {
    if (!m_bDirty && m_LastRenderMs != 0) {
        unsigned now = CTimer::Get()->GetTicks() * 1000 / 1000000;  // ms
        if (now - m_LastRenderMs >= maxAgeMs) {
            m_bDirty = true;
        }
    }
}

void CDX21Display::Render() {
    if (!m_pDisplay) return;
    if (!m_bDirty) return;
    m_bDirty = false;

    m_pDisplay->Clear();

    // Page 0 (rows 0-7)  : mode title
    // Page 1 (rows 8-15) : 7-seg big value
    // Page 2 (rows 16-23): parameter name
    // Page 3 (rows 24-31): status line
    switch (m_Mode) {
        case kModePlay:        RenderPlayMode();        break;
        case kModeEdit:        RenderEditMode();        break;
        case kModePerformance: RenderPerformanceMode(); break;
        case kModeFunction:    RenderFunctionMode();    break;
        case kModeMemory:      RenderMemoryMode();      break;
        default: {
            char line[32];
            snprintf(line, sizeof(line), "DX21  MODE %d   ", (int)m_Mode);
            DrawText6x8(0, 0, line, 16);
            DrawText6x8(0, 8, "                ", 16);
            DrawText6x8(0, 16, "                ", 16);
            DrawText6x8(0, 24, "                ", 16);
            break;
        }
    }

    // Memory-protect indicator (top-right corner, 2x2 block)
    if (m_bMemProt) {
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                m_pDisplay->SetPixel(124 + dx, dy, 1);
    }

    // Compare overlay marker (top-right, 4x2)
    if (m_bCompare) {
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 4; ++dx)
                m_pDisplay->SetPixel(120 + dx, dy, 1);
    }

    m_LastRenderMs = CTimer::Get()->GetTicks() * 1000 / 1000000;
    m_pDisplay->Show();
}
