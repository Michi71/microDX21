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
    /*  0  ALG                */  kParamAlgorithm,
    /*  1  ALGORITHM SELECT   */  kParamAlgorithm,    // long form
    /*  2  FEEDBACK           */  kParamFeedback,
    /*  3  LFO WAVE:          */  kParamLFOWave,
    /*  4  LFO SPEED          */  kParamLFOSpeed,
    /*  5  LFO DELAY          */  kParamLFODelay,
    /*  6  LFO PMD            */  kParamPMD,
    /*  7  LFO AMD            */  kParamAMD,
    /*  8  P MOD SENS.        */  kParamPMS,          // 0..7
    /*  9  A MOD SENS.        */  kParamAMS,          // 0..3
    /* 10  E BIAS SENS.       */  kParamOp0EBS,       // OP1 EBS
    /* 11  KEY VELOCITY       */  kParamOp0KVS,       // OP1 KVS
    /* 12  FREQUENCY =        */  kParamOp0CRS,       // OP1 CRS
    /* 13  DETUNE    =        */  kParamOp0DET,       // OP1 DET
    /* 14  EG  AR (OP1)       */  kParamOp0AR,
    /* 15  EG  D1R            */  kParamOp0D1R,
    /* 16  EG  D1L            */  kParamOp0D1L,
    /* 17  EG  D2R            */  kParamOp0D2R,
    /* 18  EG  RR             */  kParamOp0RR,
    /* 19  OUTPUT LEVEL       */  kParamOp0Out,
    /* 20  RATE SCALE         */  kParamOp0RS,        // OP1 RS (0..3)
    /* 21  LEVEL SCALE        */  kParamOp0LS,        // OP1 LS (0..99)
    /* 22  PEG RATE 1         */  -1,                 // OPM has no PEG
    /* 23  PEG LEVEL 1        */  -1,
    /* 24  PEG RATE 2         */  -1,
    /* 25  PEG LEVEL 2        */  -1,
    /* 26  PEG RATE 3         */  -1,
    /* 27  PEG LEVEL 3        */  -1,
    /* 28  SAW UP             */  kParamLFOWave,      // wave=0
    /* 29  SQUARE             */  kParamLFOWave,      // wave=1
    /* 30  TRIANGL            */  kParamLFOWave,      // wave=2
    /* 31  S/HOLD             */  kParamLFOWave,      // wave=3
    /* 32  EG Copy            */  -1,                 // utility
    /* 33  from OP            */  -1,                 // utility
    /* 34  LFO SYNC :         */  kParamLFOSync,
    /* 35  Memory Protected   */  -1,                 // handled by m_bMemProt
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
    /*  1  Master Tune =      */  kParamMasterTune,    // -64..+63
    /*  2  Dual Detune        */  -1,                  // not implemented yet
    /*  3  Midi Switch :      */  -1,                  // per-channel
    /*  4  Midi Ch Info:      */  -1,                  // info display
    /*  5  Midi Sy Info:      */  -1,                  // info display
    /*  6  Midi Recv Ch =     */  -1,                  // per-channel
    /*  7  Midi Omni : ON     */  -1,                  // per-channel
    /*  8  Recall Edit ?      */  -1,                  // dialog
    /*  9  Are You Sure ?     */  -1,                  // dialog
    /* 10  Init. Voice ?      */  -1,                  // dialog
    /* 11  Save to Tape ?     */  -1,                  // dialog
    /* 12  from Mem to Tape   */  -1,                  // dialog
    /* 13  Verify    Tape ?   */  -1,                  // dialog
    /* 14  Verify Tape        */  -1,                  // dialog
    /* 15  Load from Tape ?   */  -1,                  // dialog
    /* 16  from Tape to Mem   */  -1,                  // dialog
    /* 17  Load  Single ?     */  -1,                  // dialog
    /* 18  Tape # ? to Buff   */  -1,                  // dialog
    /* 19  Group to Bank ?    */  -1,                  // dialog
    /* 20  Group (1-16) ?     */  -1,                  // dialog
    /* 21  Bank (1-4) ?       */  -1,                  // dialog
    /* 22  Voice to Buff  ?   */  -1,                  // dialog
    /* 23  Mem Protect:       */  -1,                  // handled by m_bMemProt
    /* 24  Poly Mode          */  kParamPlayMode,      // Single/Dual/Split
    /* 25  Mono Mode          */  kParamMono,          // boolean
    /* 26  P Bend Range       */  kParamPitchBendRange,
    /* 27  Fingered Porta     */  kParamPortamentoMode,
    /* 28  Full Time Porta    */  kParamPortamentoMode,
    /* 29  Porta Time         */  kParamPortamentoRate,
    /* 30  Foot Volume        */  -1,                  // no setter
    /* 31  Foot Sustain:      */  -1,
    /* 32  Foot Porta  :      */  kParamPortamentoMode,
    /* 33  MW Pitch           */  -1,                  // mod wheel
    /* 34  MW Amplitude       */  -1,
    /* 35  BC Pitch           */  -1,                  // breath routing
    /* 36  BC Amplitude       */  kParamBreathAmp,
    /* 37  BC Pitch Bias      */  kParamBreathPitchBias,
    /* 38  BC EG Bias         */  kParamBreathEGBias,
    /* 39  Middle C =         */  kParamKeyOffset,     // closest analog
    /* 40  Midi Trns Ch =     */  -1,                  // per-channel
    /* 41  Midi Transmit ?    */  -1,
    /* 42  Chorus      :      */  kParamEnsemble,
    /* 43  Bend Mode =        */  kParamPBMode,
    /* 44  Key Shift =        */  kParamKeyOffset,
    /* 45  Name :             */  -1,                  // string entry
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
    , m_SplashProgress(0)
    , m_MemoryStage(0)
    , m_MemoryAction(0)
    , m_MemoryYesNo(0)
    , m_MemoryGroup(0)
    , m_MemoryResult(-1)
    , m_MemoryResultMs(0)
{
    memset(m_PageBuf, 0, sizeof(m_PageBuf));
}

CDX21Display::~CDX21Display() {
    delete m_pDisplay;
}

void CDX21Display::SetSplash(bool on) {
    m_bSplash = on;
    // Reset the fade-in progress whenever splash is toggled, so a
    // re-entry (panic / restart) starts from the top instead of
    // immediately flashing the full banner.
    m_SplashProgress = on ? 1 : 0;  // when on, start at 1 (page 0 only)
                                    // so the caller can drive the rest
    MarkDirty();
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

    // The MEMORY screen is a 3-stage state machine:
    //   stage 0: pick action  (Save / Load / Verify)
    //   stage 1: confirm      (YES / NO)
    //   stage 2: pick group   (1..16)
    //   stage 3: result-shown (OK / FAILED, 2 s)
    //
    // In stages 0 and 2, m_ParamIdx is the cursor (legacy
    // SelectParam/AdjustValue path). In stage 1, m_ParamIdx is unused
    // (we use m_MemoryYesNo for the toggle). In stage 3, no input is
    // accepted until the next click.

    // Page 0: "MEMORY  STAGE"
    if (m_MemoryStage == 1) {
        snprintf(line, sizeof(line), "%-9s YES/NO ", MODE_TITLE_MEMORY);
    } else if (m_MemoryStage == 2) {
        snprintf(line, sizeof(line), "%-9s GRP %2d ", MODE_TITLE_MEMORY, m_MemoryGroup + 1);
    } else if (m_MemoryStage == 3) {
        snprintf(line, sizeof(line), "%-9s DONE   ", MODE_TITLE_MEMORY);
    } else {
        snprintf(line, sizeof(line), "%-9s ACT %d  ", MODE_TITLE_MEMORY, m_MemoryAction);
    }
    DrawText6x8(0, 0, line, 16);

    // Page 1: big text — the action name / YES/NO / group number / result
    if (m_MemoryStage == 0) {
        // Show the action name in big 7-seg.
        static const char* kActNames[3] = { "SAVE", "LOAD", "VRFY" };
        DrawBigString(0, 8, kActNames[m_MemoryAction]);
    } else if (m_MemoryStage == 1) {
        DrawBigString(0, 8, m_MemoryYesNo ? "YES" : "NO");
    } else if (m_MemoryStage == 2) {
        // Show the group number in big 7-seg, e.g. "G05" / "G12".
        char gbuf[16];
        snprintf(gbuf, sizeof(gbuf), "G%2d", (int)(m_MemoryGroup + 1));
        DrawBigString(0, 8, gbuf);
    } else {
        // stage 3: result. Look up the result string from the adapter
        // (or fall back to a generic message).
        const char* rs = "OK";
        if (m_pAdapter) {
            rs = COPMEmuAdapter::MemoryResultString(
                (COPMEmuAdapter::MemoryResult)m_MemoryResult);
        }
        DrawBigString(0, 8, rs);
    }

    // Page 2: hint line. In stage 0/2 it's the action or group label.
    // In stage 1 it's the confirm question. In stage 3 it's blank.
    if (m_MemoryStage == 0) {
        // m_ParamIdx (0..2) is the action cursor; show its label.
        const char* an = "                ";
        if (m_ParamIdx >= 0 && m_ParamIdx < 3) {
            static const char* kLabels[3] = {
                "Save to Tape  ?",
                "Load from Tape?",
                "Verify    Tape?",
            };
            an = kLabels[m_ParamIdx];
        }
        DrawText6x8(0, 16, an, 16);
    } else if (m_MemoryStage == 1) {
        const char* q = (m_MemoryAction == 0) ? "Save to SD?"
                      : (m_MemoryAction == 1) ? "Load from SD?"
                                              : "Verify SD?";
        DrawText6x8(0, 16, q, 16);
    } else if (m_MemoryStage == 2) {
        // Show "GROUP (N) ?" as the hint for the current group.
        const char* gn = "                ";
        int labelIdx = TAPE_GROUP_FIRST + m_MemoryGroup;
        if (labelIdx >= 0 && labelIdx < TAPE_LABEL_COUNT) {
            gn = TAPE_LABELS[labelIdx];
        }
        DrawText6x8(0, 16, gn, 16);
    } else {
        // stage 3: status hint (the actual result message lives on
        // page 1 in big; the hint on page 2 is the action name).
        const char* an = "                ";
        if (m_MemoryAction >= 0 && m_MemoryAction < 3) {
            static const char* kActNames[3] = { "SAVE", "LOAD", "VERIFY" };
            an = kActNames[m_MemoryAction];
        }
        DrawText6x8(0, 16, an, 16);
    }

    // Page 3: status / hint / blank.
    if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
    } else {
        line[0] = '\0';
    }
    DrawText6x8(0, 24, line, 16);

    // Auto-clear the result display 2 s after the action ran, so the
    // user doesn't see stale "OK" forever. InvalidateIfStale() runs
    // every 5 Hz from RunCore2; we piggy-back on it.
    if (m_MemoryStage == 3 && m_MemoryResultMs != 0) {
        unsigned now = CTimer::Get()->GetTicks() * 1000 / 1000000;
        if (now - m_MemoryResultMs >= 2000) {
            // Time's up: jump back to stage 0 with a clean slate.
            m_MemoryStage = 0;
            m_MemoryResult = -1;
            m_MemoryResultMs = 0;
            ClearStatus();
            MarkDirty();
        }
    }
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

// ────────────────────────────────────────────────────────────────────
// Memory-mode state machine implementation
// ────────────────────────────────────────────────────────────────────
//
// Action index → adapter call. The numeric index is enough; the
// display uses literal action-name tables inline where the renderer
// needs them.
void CDX21Display::MemoryPickAction(int delta) {
    if (m_MemoryStage != 0) return;
    int n = 3;  // Save, Load, Verify
    int a = m_MemoryAction + delta;
    a %= n;
    if (a < 0) a += n;
    m_MemoryAction = a;
    MarkDirty();
}

void CDX21Display::MemoryToggleYesNo() {
    if (m_MemoryStage != 1) return;
    m_MemoryYesNo ^= 1;
    MarkDirty();
}

void CDX21Display::MemoryPickGroup(int delta) {
    if (m_MemoryStage != 2) return;
    int g = m_MemoryGroup + delta;
    g %= 16;
    if (g < 0) g += 16;
    m_MemoryGroup = g;
    MarkDirty();
}

// Click handler. The state machine is:
//   stage 0 + click  → stage 1 (confirm), default NO
//   stage 1 + NO + click  → stage 0
//   stage 1 + YES + click → execute, stage 3 (result)
//   stage 2 + click  → execute, stage 3
//   stage 3 + click  → stage 0 (back to pick)
void CDX21Display::MemoryConfirm() {
    if (m_MemoryStage == 0) {
        m_MemoryStage = 1;
        m_MemoryYesNo = 0;  // default NO
        MarkDirty();
    } else if (m_MemoryStage == 1) {
        if (m_MemoryYesNo == 0) {
            m_MemoryStage = 0;  // back to pick
        } else {
            m_MemoryStage = 2;  // group select
        }
        MarkDirty();
    } else if (m_MemoryStage == 2) {
        // Execute. We invoke the adapter directly. The adapter's
        // MemoryResult enum is mapped to a small int for m_MemoryResult.
        if (m_pAdapter) {
            COPMEmuAdapter::MemoryResult r;
            switch (m_MemoryAction) {
                case 0:  r = m_pAdapter->saveRamBankToFile(m_MemoryGroup); break;
                case 1:  r = m_pAdapter->loadRamBankFromFile(m_MemoryGroup); break;
                case 2:  r = m_pAdapter->verifyRamBank(m_MemoryGroup);     break;
                default: r = COPMEmuAdapter::MemoryResult::NotImplemented; break;
            }
            m_MemoryResult = (int)r;
            // Show a status string for ~2 s.
            SetStatus(COPMEmuAdapter::MemoryResultString(r));
        } else {
            m_MemoryResult = (int)COPMEmuAdapter::MemoryResult::NoFS;
            SetStatus("NO ADAPTER");
        }
        m_MemoryResultMs = CTimer::Get()->GetTicks() * 1000 / 1000000;
        m_MemoryStage = 3;
        MarkDirty();
    } else {
        // stage 3: back to pick
        m_MemoryStage = 0;
        m_MemoryResult = -1;
        ClearStatus();
        MarkDirty();
    }
}

void CDX21Display::RenderSplashMode() {
    // Power-on splash with a top-down fade-in. The original DX21's
    // 2x16 character LCD was actually initialised row-by-row by the
    // 6803 firmware, so a per-page reveal matches the real boot
    // sequence better than a hard 2 s "everything on".
    //
    // m_SplashProgress is 0..4 (4 = full banner). Render() also
    // calls m_pDisplay->Clear() before us, so a page that hasn't
    // been "unlocked" yet is just blank.
    //
    // The 4 lines on the 128x32 OLED:
    //   page 0 (rows  0..7 ) : "*  YAMAHA  *"
    //   page 1 (rows  8..15) : 7-seg "DX21"
    //   page 2 (rows 16..23) : "* SYNTHESIZER *"
    //   page 3 (rows 24..31) : "v0.1.0  INIT..."
    if (m_SplashProgress >= 1) DrawText6x8(0, 0,  "*  YAMAHA  *",   16);
    if (m_SplashProgress >= 2) DrawBigString  (0, 8,  "DX21");
    if (m_SplashProgress >= 3) DrawText6x8(0, 16, "* SYNTHESIZER *", 16);
    if (m_SplashProgress >= 4) DrawText6x8(0, 24, "v0.1.0  INIT...", 16);
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
