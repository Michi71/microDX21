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
    /*  0  ALG                */  kParamAlgorithm,  // 13
    /*  1  ALGORITHM SELECT   */  kParamAlgorithm,  // 13 (long form)
    /*  2  FEEDBACK           */  kParamFeedback,    // 14
    /*  3  LFO WAVE:          */  kParamLFOWave,      // 16
    /*  4  LFO SPEED          */  kParamLFOSpeed,     // 9
    /*  5  LFO DELAY          */  kParamLFODelay,     // 10
    /*  6  LFO PMD            */  kParamPMD,          // 11
    /*  7  LFO AMD            */  kParamAMD,          // 12
    /*  8  P MOD SENS.        */  -1,                 // (no getter)
    /*  9  A MOD SENS.        */  -1,
    /* 10  E BIAS SENS.       */  -1,
    /* 11  KEY VELOCITY       */  -1,
    /* 12  FREQUENCY =        */  -1,
    /* 13  DETUNE    =        */  -1,
    /* 14  EG  AR (OP1)       */  kParamOp0AR,        // 17
    /* 15  EG  D1R            */  kParamOp0D1R,
    /* 16  EG  D1L            */  kParamOp0D1L,
    /* 17  EG  D2R            */  -1,
    /* 18  EG  RR             */  -1,
    /* 19  OUTPUT LEVEL       */  kParamOp0Out,       // 20
    /* 20  RATE SCALE         */  -1,
    /* 21  LEVEL SCALE        */  -1,
    /* 22  PEG RATE 1         */  -1,
    /* 23  PEG LEVEL 1        */  -1,
    /* 24  PEG RATE 2         */  -1,
    /* 25  PEG LEVEL 2        */  -1,
    /* 26  PEG RATE 3         */  -1,
    /* 27  PEG LEVEL 3        */  -1,
    /* 28  SAW UP             */  kParamLFOWave,      // 16 (sets wave=0)
    /* 29  SQUARE             */  kParamLFOWave,      // 16 (sets wave=1)
    /* 30  TRIANGL            */  kParamLFOWave,      // 16 (sets wave=2)
    /* 31  S/HOLD             */  kParamLFOWave,      // 16 (sets wave=3)
    /* 32  EG Copy            */  -1,                 // utility, not a param
    /* 33  from OP            */  -1,
    /* 34  LFO SYNC :         */  kParamLFOSync,      // 15
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
    , m_bSplash(false)
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
        m_pAdapter->getInstrumentName(nameBuf, sizeof(nameBuf));
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
        m_pAdapter->getInstrumentName(nameBuf, sizeof(nameBuf));
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

// ────────────────────────────────────────────────────────────────────
// Encoder action API
// ────────────────────────────────────────────────────────────────────
//
// Number of selectable items per mode. Mirrors the kEditToAdapter /
// kFunctionToAdapter / kPerformanceToAdapter tables above. Used by
// the encoder for wrap-around math and by the renderer to clamp the
// param index display ("N/MAX").
int CDX21Display::GetParamCountForMode() const {
    switch (m_Mode) {
        case kModePlay:        return 128;                       // 1..128 voices
        case kModeEdit:        return EDIT_PARAM_COUNT;
        case kModePerformance: return PLAY_LABEL_COUNT;
        case kModeFunction:    return FUNCTION_COUNT;
        case kModeMemory:      return TAPE_LABEL_COUNT;
    }
    return 0;
}

// SelectParam moves the per-mode cursor by `delta` (typically ±1 per
// encoder detent), wrapping at both ends. Pure UI state — does not
// touch the synth. The renderer redraws the next time it ticks.
void CDX21Display::SelectParam(int delta) {
    int maxIdx = GetParamCountForMode();
    if (maxIdx <= 0) return;
    int idx = m_ParamIdx + delta;
    // Wrap modulo maxIdx. C's % on negatives can yield negative
    // results, so the explicit two-step handles both directions.
    idx %= maxIdx;
    if (idx < 0) idx += maxIdx;
    m_ParamIdx = idx;
    MarkDirty();
}

// AdjustValue does the per-mode meaningful change for the current
// cursor position. Returns the new absolute value when one is
// computable, or -1 when the cursor points at a "n/a" param /
// un-bound adapter / mode without live binding.
//
// Behaviour matrix:
//   PLAY         : m_ParamIdx 0..127 → voice 1..128 (kParamInstrument)
//   EDIT         : kEditToAdapter[m_ParamIdx]   ±delta → m_pAdapter
//   FUNCTION     : kFunctionToAdapter[m_ParamIdx] ±delta → m_pAdapter
//   PERFORMANCE  : m_ParamIdx 0..127 → voice 1..128 (kParamInstrument)
//   MEMORY       : m_ParamIdx 0..TAPE_LABEL_COUNT-1 → cursor only
//                  (tape dialogs are non-numeric; rotate and confirm)
//
// "delta" is interpreted in the natural units of the parameter
// (1 raw VCED byte, 1 voice number, 1 waveform step, etc.). The
// adapter's setParameter() clamps to 0..1 and the underlying synth
// method rounds to the right integer range.
int CDX21Display::AdjustValue(int delta) {
    if (!m_pAdapter) return -1;
    int kp = -1;

    switch (m_Mode) {
        case kModePlay:
        case kModePerformance:
            // Both modes use the same voice cursor. PLAY shows
            // 1..128, PERFORMANCE uses the same range (we don't have
            // a separate 32-perf memory store yet — that's a future
            // step once the user wires up a second encoder or
            // button). Delta moves the voice by ±1.
            kp = kParamInstrument;
            break;

        case kModeEdit:
            if (m_ParamIdx < 0 || m_ParamIdx >= EDIT_PARAM_COUNT) return -1;
            kp = kEditToAdapter[m_ParamIdx];
            break;

        case kModeFunction:
            if (m_ParamIdx < 0 || m_ParamIdx >= FUNCTION_COUNT) return -1;
            kp = kFunctionToAdapter[m_ParamIdx];
            break;

        case kModeMemory:
            // Tape dialogs are not numeric. The cursor-only mode
            // (SelectParam) covers navigation; the user confirms
            // via COMPARE (double-click). We don't write to the
            // synth here.
            return -1;
    }

    if (kp < 0) return -1;  // EDIT/FUNCTION entry with no live binding

    // For Play/Performance the cursor index 0..127 maps directly to
    // a normalised 0..1 program number. For EDIT/FUNCTION we add
    // delta to the current normalised value and clamp.
    float current = m_pAdapter->getParameter(kp);
    float next    = current;
    if (m_Mode == kModePlay || m_Mode == kModePerformance) {
        // step per detent = 1 / (N-1), N = num programs.
        int n = m_pAdapter->getNumPresets();
        if (n <= 1) return -1;
        float step = 1.0f / (float)(n - 1);
        next = current + (float)delta * step;
    } else {
        // Most EDIT/FUNCTION params map a 0..1 float onto a small
        // integer range. We step by 1/255 of the range per detent,
        // which is ≈1 raw VCED byte — close to the original DX21
        // "value-rotate" feel.
        next = current + (float)delta * (1.0f / 255.0f);
    }
    if (next < 0.0f) next = 0.0f;
    if (next > 1.0f) next = 1.0f;
    m_pAdapter->setParameter(kp, next);

    // Mirror the change into the m_Value display field so the
    // renderer's "=NN" / big number can show it without re-reading.
    m_Value = (int)(next * 255.0f + 0.5f);
    if (m_Value < 0)   m_Value = 0;
    if (m_Value > 255) m_Value = 255;
    MarkDirty();
    return m_Value;
}

void CDX21Display::RenderSplashMode() {
    // Power-on splash. Original DX21 boot banner on the 2x16 character
    // LCD was:
    //   row 0: "*  YAMAHA DX21 *"
    //   row 1: "*  SYNTHESIZER *"
    // The 128x32 OLED has 4 pages of 8 px, so we put the wordmark
    // on page 0, the 7-seg "DX21" mark in big on page 1, the
    // sub-wordmark on page 2, and the version + init hint on page 3.
    DrawText6x8(0, 0,  "*  YAMAHA  *",   16);
    DrawBigString  (0, 8,  "DX21");
    DrawText6x8(0, 16, "* SYNTHESIZER *", 16);
    DrawText6x8(0, 24, "v0.1.0  INIT...", 16);
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
    if (m_bSplash) {
        RenderSplashMode();
    } else {
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
