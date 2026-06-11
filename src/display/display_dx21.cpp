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
    /* 22  PEG RATE 1         */  kParamPEGR1,        // firmware-side PEG
    /* 23  PEG LEVEL 1        */  kParamPEGL1,
    /* 24  PEG RATE 2         */  kParamPEGR2,
    /* 25  PEG LEVEL 2        */  kParamPEGL2,
    /* 26  PEG RATE 3         */  kParamPEGR3,
    /* 27  PEG LEVEL 3        */  kParamPEGL3,
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

// EDIT_PARAM_NAMES indices of the EG-Copy utility entries. Both
// behave identically: rotation picks the source OP, click copies
// into the currently selected m_EditOp (TriggerEditAction).
static constexpr int kEditIdxEgCopy     = 32;  // "EG Copy"
static constexpr int kEditIdxEgCopyFrom = 33;  // "from OP"

static inline bool IsEgCopyEntry(int idx) {
    return idx == kEditIdxEgCopy || idx == kEditIdxEgCopyFrom;
}

// The table above maps the per-operator entries to OP1 (kParamOp0*).
// The DX21ParamIndex enum lays the other operators out at fixed
// strides behind OP1 — 5 consecutive core params per op
// (kParamOp0AR..kParamOp0CRS), then 8 consecutive extended params
// per op (kParamOp0D2R..kParamOp0DET) — so OP2..OP4 are a constant
// offset away. ResolveEditParam() applies that offset for the
// operator currently selected via CDX21Display::m_EditOp (cycled by
// triple-click in EDIT mode). Non-per-op entries pass through.
static int ResolveEditParam(int kp, int op) {
    if (op <= 0 || kp < 0) return kp;
    if (kp >= kParamOp0AR  && kp <= kParamOp0CRS) return kp + op * 5;
    if (kp >= kParamOp0D2R && kp <= kParamOp0DET) return kp + op * 8;
    return kp;
}

// True if this EDIT entry targets a per-operator parameter (used by
// the renderer to decide whether to show the OP indicator).
static bool IsPerOpEditParam(int kp) {
    return (kp >= kParamOp0AR  && kp <= kParamOp0CRS) ||
           (kp >= kParamOp0D2R && kp <= kParamOp0DET);
}

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
    /*  2  Dual Detune        */  kParamDualDetune,    // 0..99 (DUAL side B)
    /*  3  Midi Switch :      */  kParamMidiSwitch,    // master MIDI receive
    /*  4  Midi Ch Info:      */  kParamCHInfo,
    /*  5  Midi Sy Info:      */  kParamSysInfo,
    /*  6  Midi Recv Ch =     */  kParamReceiveCh,     // 0=Omni, 1..16
    /*  7  Midi Omni : ON     */  kParamReceiveCh,     // same state: Omni = ch 0
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
    /* 30  Foot Volume        */  kParamFootVolume,    // CC#4 enable
    /* 31  Foot Sustain:      */  kParamFootSustain,   // CC#64 enable
    /* 32  Foot Porta  :      */  kParamFootPorta,     // CC#65 jack enable
    /* 33  MW Pitch           */  kParamMWPitchRange,  // 0..99 (B8)
    /* 34  MW Amplitude       */  kParamMWAmpRange,    // 0..99 (B9)
    /* 35  BC Pitch           */  kParamBreathPitch,   // 0..99 (B10)
    /* 36  BC Amplitude       */  kParamBreathAmp,
    /* 37  BC Pitch Bias      */  kParamBreathPitchBias,
    /* 38  BC EG Bias         */  kParamBreathEGBias,
    /* 39  Middle C =         */  kParamKeyOffset,     // closest analog
    /* 40  Midi Trns Ch =     */  kParamTransmitCh,    // 0=off, 1..15
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
static int ReadEditValue(COPMEmuAdapter* a, int paramIdx, int editOp) {
    if (!a) return -1;
    if (paramIdx < 0 || paramIdx >= EDIT_PARAM_COUNT) return -1;
    int kp = ResolveEditParam(kEditToAdapter[paramIdx], editOp);
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

    // Current entry's adapter binding (OP-resolved) — used for the
    // OP indicator and the page-3 display string.
    const int safeIdx = (m_ParamIdx >= 0 && m_ParamIdx < EDIT_PARAM_COUNT)
        ? m_ParamIdx : 0;
    const int kpRaw = kEditToAdapter[safeIdx];

    // Page 0: "EDIT  N/36" — with "OPn" indicator when the current
    // entry is a per-operator parameter (triple-click cycles n).
    if (IsPerOpEditParam(kpRaw)) {
        snprintf(line, sizeof(line), "%-5s OP%d %2d/%-2d  ",
                 MODE_TITLE_EDIT, m_EditOp + 1, m_ParamIdx + 1,
                 EDIT_PARAM_COUNT);
    } else {
        snprintf(line, sizeof(line), "%-9s %2d/%-2d     ",
                 MODE_TITLE_EDIT, m_ParamIdx + 1, EDIT_PARAM_COUNT);
    }
    DrawText6x8(0, 0, line, 16);

    // Page 1: big 3-digit value (read from adapter, or fallback to m_Value)
    int value = ReadEditValue(m_pAdapter, m_ParamIdx, m_EditOp);
    if (IsEgCopyEntry(m_ParamIdx)) value = m_EgCopySrc + 1;  // source OP
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
    } else if (IsEgCopyEntry(m_ParamIdx)) {
        // EG Copy: show the pending source → destination route.
        snprintf(line, sizeof(line), "from OP%d to OP%d ",
                 m_EgCopySrc + 1, m_EditOp + 1);
    } else {
        // Show the adapter-side display string if available.
        if (m_pAdapter) {
            std::string s = m_pAdapter->getParameterDisplay(
                ResolveEditParam(kpRaw, m_EditOp));
            snprintf(line, sizeof(line), "%-16.16s", s.c_str());
        } else {
            line[0] = '\0';
        }
    }
    DrawText6x8(0, 24, line, 16);
}

void CDX21Display::RenderPerformanceMode() {
    char line[32];

    // The 32 performance memories. m_PerfSlot is the slot the last
    // AdjustValue() applied; the renderer reads the slot's stored
    // data through the adapter. Empty slots render as "-- EMPTY --".
    char perfName[17] = {0};
    int  playMode = -1, voiceA = -1, voiceB = -1;
    bool valid = false;
    if (m_pAdapter) {
        valid = m_pAdapter->GetPerformanceInfo(m_PerfSlot, perfName,
                                               sizeof(perfName),
                                               &playMode, &voiceA, &voiceB);
    }

    // Page 0: "PERFORM  NN/32" (+ play-mode tag like the original LCD)
    static const char* kModeTag[3] = { "SI", "DU", "SP" };
    const char* tag = (valid && playMode >= 0 && playMode < 3)
        ? kModeTag[playMode] : "--";
    snprintf(line, sizeof(line), "%-7s%s %2d/32  ",
             MODE_TITLE_PERFORMANCE, tag, m_PerfSlot + 1);
    DrawText6x8(0, 0, line, 16);

    // Page 1: performance name (or "BUFF" when COMPARE is active)
    if (m_bCompare) {
        DrawBigString(0, 8, "BUFF");
    } else if (valid) {
        DrawBigString(0, 8, perfName);
    } else {
        DrawBigString(0, 8, "----");
    }

    // Page 2: programmed voices "A:NNN B:NNN" (manual: lower LCD line)
    if (valid) {
        snprintf(line, sizeof(line), "A:%3d  B:%3d    ",
                 voiceA + 1, voiceB + 1);
    } else {
        snprintf(line, sizeof(line), "-- EMPTY --     ");
    }
    DrawText6x8(0, 16, line, 16);

    // Page 3: status / hint
    if (m_Status) {
        snprintf(line, sizeof(line), "%-16.16s", m_Status);
        DrawText6x8(0, 24, line, 16);
    } else {
        DrawText6x8(0, 24, "EDIT=APPLY      ", 16);
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

    // Page 2: value (numeric, ON/OFF, "n/a") — or the name editor.
    if (m_bNameEdit) {
        // "Name:XXXXXXXXXX" with an underline cursor below the
        // character being edited.
        snprintf(line, sizeof(line), "Name:%-10.10s ", m_NameBuf);
        DrawText6x8(0, 16, line, 16);
        DrawCursor((5 + m_NamePos) * 6, 23, 6, 1);
    } else {
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
    }

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

    // The MEMORY screen is a multi-stage state machine:
    //   stage 0: pick action  (7 actions, see header)
    //   stage 1: confirm      (YES / NO)
    //   stage 2: first picker (group / slot / ROM voice)
    //   stage 4: second picker (bank / destination slot; actions 4/5)
    //   stage 3: result-shown (OK / FAILED, 2 s)

    static const char* kActBig[kMemoryActionCount] =
        { "SAVE", "LOAD", "VRFY", "STOR", "ROMG", "ROMV", "PERF" };
    static const char* kActHint[kMemoryActionCount] = {
        "Save to SD    ?",
        "Load from SD  ?",
        "Verify SD     ?",
        "Store Voice   ?",
        "Group to Bank ?",
        "Voice to Slot ?",
        "Store Perform.?",
    };

    // Page 0: "MEMORY  <stage>"
    if (m_MemoryStage == 1) {
        snprintf(line, sizeof(line), "%-9s YES/NO ", MODE_TITLE_MEMORY);
    } else if (m_MemoryStage == 2) {
        snprintf(line, sizeof(line), "%-9s PICK%3d", MODE_TITLE_MEMORY, m_MemoryGroup + 1);
    } else if (m_MemoryStage == 4) {
        snprintf(line, sizeof(line), "%-9s DEST%3d", MODE_TITLE_MEMORY, m_MemorySlot + 1);
    } else if (m_MemoryStage == 3) {
        snprintf(line, sizeof(line), "%-9s DONE   ", MODE_TITLE_MEMORY);
    } else {
        snprintf(line, sizeof(line), "%-9s ACT %d  ", MODE_TITLE_MEMORY, m_MemoryAction + 1);
    }
    DrawText6x8(0, 0, line, 16);

    // Page 1: big text — action name / YES/NO / picker value / result.
    if (m_MemoryStage == 0) {
        DrawBigString(0, 8, kActBig[m_MemoryAction]);
    } else if (m_MemoryStage == 1) {
        DrawBigString(0, 8, m_MemoryYesNo ? "YES" : "NO");
    } else if (m_MemoryStage == 2) {
        char gbuf[16];
        snprintf(gbuf, sizeof(gbuf), "%3d", (int)(m_MemoryGroup + 1));
        DrawBigString(0, 8, gbuf);
    } else if (m_MemoryStage == 4) {
        char gbuf[16];
        snprintf(gbuf, sizeof(gbuf), "%3d", (int)(m_MemorySlot + 1));
        DrawBigString(0, 8, gbuf);
    } else {
        // stage 3: result string from the adapter.
        const char* rs = "OK";
        if (m_pAdapter) {
            rs = COPMEmuAdapter::MemoryResultString(
                (COPMEmuAdapter::MemoryResult)m_MemoryResult);
        }
        DrawBigString(0, 8, rs);
    }

    // Page 2: hint line.
    if (m_MemoryStage == 0 || m_MemoryStage == 1 || m_MemoryStage == 3) {
        DrawText6x8(0, 16, kActHint[m_MemoryAction], 16);
    } else if (m_MemoryStage == 2) {
        if (m_MemoryAction == 5 && m_pAdapter) {
            // ROM voice picker: show the voice's name.
            snprintf(line, sizeof(line), "%-16.16s",
                     m_pAdapter->GetRomVoiceName(m_MemoryGroup));
            DrawText6x8(0, 16, line, 16);
        } else if (m_MemoryAction == 3 || m_MemoryAction == 6) {
            DrawText6x8(0, 16, "Slot (1-32) ?   ", 16);
        } else if (m_MemoryAction == 4) {
            DrawText6x8(0, 16, "Group (1-16) ?  ", 16);
        } else {
            // SD actions: bank-group label from TAPE_LABELS.
            const char* gn = "Group (1-16) ?  ";
            int labelIdx = TAPE_GROUP_FIRST + m_MemoryGroup;
            if (labelIdx >= 0 && labelIdx < TAPE_LABEL_COUNT) {
                gn = TAPE_LABELS[labelIdx];
            }
            DrawText6x8(0, 16, gn, 16);
        }
    } else {  // stage 4
        DrawText6x8(0, 16,
                    (m_MemoryAction == 4) ? "Bank (1-4) ?    "
                                          : "Slot (1-32) ?   ", 16);
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
        case kModePerformance: return 32;                        // 32 performances
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
            // Voice select 1..128 (kParamInstrument).
            kp = kParamInstrument;
            break;

        case kModePerformance: {
            // Step the performance slot 1..32 and apply it. Empty
            // slots are reported in the status line; the slot cursor
            // still moves so the user can reach stored slots beyond
            // an empty one.
            int slot = m_PerfSlot + delta;
            slot %= 32;
            if (slot < 0) slot += 32;
            m_PerfSlot = slot;
            bool ok = m_pAdapter->ApplyPerformance(slot);
            SetStatus(ok ? "PERF APPLIED" : "EMPTY PERF");
            MarkDirty();
            return slot + 1;
        }

        case kModeEdit:
            if (m_ParamIdx < 0 || m_ParamIdx >= EDIT_PARAM_COUNT) return -1;
            // "EG Copy" / "from OP": rotation selects the SOURCE
            // operator (1..4). The copy itself fires on click
            // (TriggerEditAction). No synth write here.
            if (IsEgCopyEntry(m_ParamIdx)) {
                int src = (m_EgCopySrc + delta) & 3;
                m_EgCopySrc = src;
                char msg[24];
                snprintf(msg, sizeof(msg), "FROM OP%d>OP%d",
                         src + 1, m_EditOp + 1);
                SetStatus(msg);
                MarkDirty();
                // -1: the input layer must not overwrite our status
                // with its generic "VAL=" message.
                return -1;
            }
            kp = ResolveEditParam(kEditToAdapter[m_ParamIdx], m_EditOp);
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
// EDIT-mode action entries. Click on "EG Copy" / "from OP" executes
// the EG copy from the rotation-selected source operator into the
// triple-click-selected destination (m_EditOp). Memory Protect and
// src == dst are rejected by the engine.
bool CDX21Display::TriggerEditAction() {
    if (!m_pAdapter || m_Mode != DX21UI::kModeEdit) return false;
    if (!IsEgCopyEntry(m_ParamIdx)) return false;

    char msg[24];
    if (m_pAdapter->TriggerEgCopy(m_EgCopySrc, m_EditOp)) {
        snprintf(msg, sizeof(msg), "EG OP%d>OP%d OK",
                 m_EgCopySrc + 1, m_EditOp + 1);
    } else if (m_EgCopySrc == m_EditOp) {
        snprintf(msg, sizeof(msg), "SRC=DST OP%d", m_EditOp + 1);
    } else {
        snprintf(msg, sizeof(msg), "EG COPY FAILED");
    }
    SetStatus(msg);
    MarkDirty();
    return true;  // click consumed — don't cycle the mode
}

bool CDX21Display::TriggerFunctionAction() {
    if (!m_pAdapter || m_Mode != DX21UI::kModeFunction) return false;
    int idx = m_ParamIdx;
    if (idx < 0 || idx >= FUNCTION_COUNT) return false;
    if (kFunctionToAdapter[idx] != -1) return false;  // not an action

    // Hard-coded mapping of FUNCTION entry index → adapter trigger.
    // The DX21's FUNCTION table has these at fixed positions.
    switch (idx) {
        case 8:  // A9: Recall Edit ?
            m_pAdapter->TriggerLoadEditRecall();
            SetStatus("EDIT RECALLED");
            MarkDirty();
            return true;
        case 10: // A10: Init. Voice ?
            m_pAdapter->TriggerInitVoice();
            SetStatus("VOICE INITIALIZED");
            MarkDirty();
            return true;
        case 11: // A11: Save to Tape ?  (this is in MEMORY mode now, but
                // FUNCTION index 11 is still a dialog. Skip here —
                // handled by the MEMORY-mode state machine instead.)
            return false;
        case 15: // A14: Load from Tape ?
            return false;  // ditto
        case 18: // A17: Tape # ? to Buff
            return false;  // ditto
        case 41: // A8: MIDI Transmit ?
            if (m_pAdapter->TriggerBulkTransmit()) {
                SetStatus("BULK TRANSMITTED");
            } else {
                SetStatus("BULK FAILED");
            }
            MarkDirty();
            return true;
        case 45: { // B16: Name : — enter the voice-name editor.
            char cur[17] = {0};
            m_pAdapter->getInstrumentName(cur, sizeof(cur));
            bool ended = false;
            for (int i = 0; i < 10; ++i) {
                if (!ended && cur[i] == '\0') ended = true;
                char c = ended ? ' ' : cur[i];
                m_NameBuf[i] = (c >= 0x20 && c < 0x7F) ? c : ' ';
            }
            m_NameBuf[10] = '\0';
            m_NamePos = 0;
            m_bNameEdit = true;
            SetStatus("NAME EDIT");
            MarkDirty();
            return true;
        }
    }
    return false;
}

// ────────────────────────────────────────────────────────────────────
// Voice-name editor (FUNCTION "Name :")
// ────────────────────────────────────────────────────────────────────

// Character set cycled by encoder rotation. Mirrors the characters
// printed in white on the original panel (plus lowercase).
static const char kNameChars[] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-./";
static const int kNameCharCount = (int)sizeof(kNameChars) - 1;

void CDX21Display::NameEditChangeChar(int delta) {
    if (!m_bNameEdit) return;
    if (m_NamePos < 0 || m_NamePos > 9) return;
    char cur = m_NameBuf[m_NamePos];
    int idx = 0;
    for (int i = 0; i < kNameCharCount; ++i) {
        if (kNameChars[i] == cur) { idx = i; break; }
    }
    idx = (idx + delta) % kNameCharCount;
    if (idx < 0) idx += kNameCharCount;
    m_NameBuf[m_NamePos] = kNameChars[idx];
    MarkDirty();
}

bool CDX21Display::NameEditClick() {
    if (!m_bNameEdit) return false;
    if (m_NamePos < 9) {
        ++m_NamePos;
        MarkDirty();
        return false;
    }
    // Last position: commit. Trim trailing spaces and write the name
    // through the adapter (Memory Protect is enforced in the engine).
    char buf[11];
    memcpy(buf, m_NameBuf, sizeof(buf));
    for (int i = 9; i >= 0 && buf[i] == ' '; --i) buf[i] = '\0';
    bool ok = m_pAdapter && m_pAdapter->SetVoiceName(buf);
    m_bNameEdit = false;
    m_NamePos = 0;
    SetStatus(ok ? "NAME STORED" : "NAME REJECTED");
    MarkDirty();
    return true;
}

// Range of the FIRST picker (stage 2) per action: SD actions pick a
// group 1-16, Store Voice / Store Perf pick a slot 1-32, ROM
// Grp→Bank picks the ROM group 1-16, ROM Voice→Slot picks the ROM
// voice 1-128.
static int MemoryPicker1Range(int action) {
    switch (action) {
        case 0: case 1: case 2: return 16;   // SD group
        case 3:                 return 32;   // RAM slot
        case 4:                 return 16;   // ROM group
        case 5:                 return 128;  // ROM voice
        case 6:                 return 32;   // performance slot
    }
    return 16;
}

// Range of the SECOND picker (stage 4): bank 1-4 for ROM Grp→Bank,
// RAM slot 1-32 for ROM Voice→Slot.
static int MemoryPicker2Range(int action) {
    return (action == 4) ? 4 : 32;
}

void CDX21Display::MemoryPickAction(int delta) {
    if (m_MemoryStage != 0) return;
    int n = kMemoryActionCount;
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

void CDX21Display::MemoryPickSlot(int delta) {
    if (m_MemoryStage != 4) return;
    int n = MemoryPicker2Range(m_MemoryAction);
    int s = m_MemorySlot + delta;
    s %= n;
    if (s < 0) s += n;
    m_MemorySlot = s;
    MarkDirty();
}

void CDX21Display::MemoryPickGroup(int delta) {
    if (m_MemoryStage != 2) return;
    int n = MemoryPicker1Range(m_MemoryAction);
    int g = m_MemoryGroup + delta;
    g %= n;
    if (g < 0) g += n;
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
        return;
    }
    if (m_MemoryStage == 1) {
        if (m_MemoryYesNo == 0) {
            m_MemoryStage = 0;  // back to pick
        } else {
            m_MemoryStage = 2;  // first picker
            m_MemoryGroup %= MemoryPicker1Range(m_MemoryAction);
        }
        MarkDirty();
        return;
    }
    if (m_MemoryStage == 2 && (m_MemoryAction == 4 || m_MemoryAction == 5)) {
        // ROM Grp→Bank / ROM Voice→Slot need the destination picker.
        m_MemoryStage = 4;
        m_MemorySlot %= MemoryPicker2Range(m_MemoryAction);
        MarkDirty();
        return;
    }
    if (m_MemoryStage == 2 || m_MemoryStage == 4) {
        // Execute. The adapter's MemoryResult enum is mapped to a
        // small int for m_MemoryResult.
        if (m_pAdapter) {
            COPMEmuAdapter::MemoryResult r;
            switch (m_MemoryAction) {
                case 0:  r = m_pAdapter->saveRamBankToFile(m_MemoryGroup);   break;
                case 1:  r = m_pAdapter->loadRamBankFromFile(m_MemoryGroup); break;
                case 2:  r = m_pAdapter->verifyRamBank(m_MemoryGroup);       break;
                case 3:  r = m_pAdapter->StoreVoice(m_MemoryGroup);          break;
                case 4:  r = m_pAdapter->LoadRomGroup(m_MemoryGroup, m_MemorySlot); break;
                case 5:  r = m_pAdapter->LoadRomVoice(m_MemoryGroup, m_MemorySlot); break;
                case 6:  r = m_pAdapter->StorePerformance(m_MemoryGroup);    break;
                default: r = COPMEmuAdapter::MemoryResult::NotImplemented;   break;
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
        return;
    }
    // stage 3: back to pick
    m_MemoryStage = 0;
    m_MemoryResult = -1;
    ClearStatus();
    MarkDirty();
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
