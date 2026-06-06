// ============================================================================
// display_dx21.h
//
// CDX21Display - thin wrapper around CSSD1305SPIDisplay (128x32 monochrome)
// that renders the 6 DX21 modes (PLAY, EDIT, PERFORMANCE, FUNCTION, MEMORY,
// COMPARE) using the original ROM's display layout from
// src/opm/firmware/dx21_rom_v1_5.asm.
//
// Layout (128x32 = 4 pages, 8 rows per page):
//
//   +--------------------------------------------------+
//   | <MODE TITLE 16 chars>     (page 0,  rows 0-7)   |  <- mode title
//   | <VALUE 8x16 big digit>     (page 0+1, glyph 16)  |
//   | <PARAM NAME 16 chars>     (page 2,  rows 16-23) |  <- param name
//   | <STATUS 16 chars>         (page 3,  rows 24-31) |  <- status line
//   +--------------------------------------------------+
//
// Top 16 rows: 16 chars of small text (mode title) using font6x8 OR
//              a 7-segment digit/value using dx21_ui_7seg.h.
//
// Bottom 16 rows: 16 chars of small text (param name + status).
//
// Threading: this class is not internally locked. The caller (kernel.cpp's
// display thread on core 2) is responsible for not calling Render() from
// more than one context. Audio core must NOT touch this class.
// ============================================================================

#ifndef _display_dx21_h
#define _display_dx21_h

#include "ssd1305spidisplay.h"
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <circle/types.h>
#include <circle/timer.h>

#include "../opm/dx21_ui_strings.h"
#include "../opm/dx21_ui_7seg.h"

class CDX21Display {
public:
    static const unsigned kWidth  = 128;
    static const unsigned kHeight = 32;

    // Single-encoder configuration. Pin assignments follow the BCM numbering
    // (same as the rest of microDX21). Override via config.txt if needed.
    struct Config {
        unsigned nDCPin       = 24;   // SPI Data/Command
        unsigned nResetPin    = 25;   // Optional, set to GPIO_PINS for none
        unsigned nChipSelect  = 0;    // SPI CE0
        unsigned nClockSpeed  = 8000000;  // 8 MHz is safe for SSD1305
        unsigned nCPOL        = 0;
        unsigned nCPHA        = 0;
        unsigned nSPIBus      = 0;
    };

    CDX21Display(CSPIMaster* pSPI, const Config& cfg);
    ~CDX21Display();

    bool Initialize();

    // Power on/off the panel.
    void On()  { if (m_pDisplay) m_pDisplay->On(); }
    void Off() { if (m_pDisplay) m_pDisplay->Off(); }

    // The current display mode (matches the original ROM's $1C65 byte).
    void SetMode(DX21UI::Mode mode)         { m_Mode = mode; MarkDirty(); }
    DX21UI::Mode GetMode() const            { return m_Mode; }

    // Sub-state: which parameter index is selected in EDIT/FUNCTION, or
    // which voice number in PLAY. Range depends on mode.
    void SetParamIndex(int idx)             { m_ParamIdx = idx; MarkDirty(); }
    int  GetParamIndex() const              { return m_ParamIdx; }

    // Current value of the selected parameter. -1 = "no numeric value,
    // show string instead" (e.g. ON/OFF in a toggle parameter).
    void SetValue(int v)                    { m_Value = v; MarkDirty(); }
    int  GetValue() const                   { return m_Value; }

    // For string-valued params (chorus on/off, lfo wave, etc.).
    void SetValueString(const char* s)      { m_ValueStr = s; MarkDirty(); }
    const char* GetValueString() const      { return m_ValueStr; }

    // Voice name shown in PLAY mode (up to 16 chars).
    void SetVoiceName(const char* name)     { m_VoiceName = name; MarkDirty(); }
    const char* GetVoiceName() const        { return m_VoiceName; }

    // Voice/program number in PLAY mode (1..128).
    void SetVoiceNumber(int n)              { m_VoiceNum = n; MarkDirty(); }
    int  GetVoiceNumber() const             { return m_VoiceNum; }

    // For status line (page 3): errors, MIDI status, memory protect warning.
    void SetStatus(const char* s)           { m_Status = s; MarkDirty(); }
    void ClearStatus()                      { m_Status = nullptr; MarkDirty(); }

    // Compare overlay flag (ROM's $1C65 compare bit).
    void SetCompare(bool on)                { m_bCompare = on; MarkDirty(); }
    bool GetCompare() const                 { return m_bCompare; }

    // Memory protect flag.
    void SetMemoryProtect(bool on)          { m_bMemProt = on; MarkDirty(); }
    bool GetMemoryProtect() const           { return m_bMemProt; }

    // Re-render the framebuffer. Call this from the display thread after any
    // Set* call. Internally skips the work if nothing changed and the
    // framebuffer is up-to-date on the panel.
    void Render();

    // Force a full redraw on next Render() (e.g. after wakeup).
    void Invalidate()                       { m_bDirty = true; }

    // Direct access for tests / custom draw.
    CSSD1305SPIDisplay* GetDisplay()        { return m_pDisplay; }

private:
    void MarkDirty()                        { m_bDirty = true; }
    void ClearFrameBuffer();
    void DrawText6x8(int x, int y, const char* s, int maxChars = 16,
                     bool inverted = false);
    void DrawText6x8_Page(int page, const char* s, bool inverted = false);
    void DrawGlyph7Seg(int gx, int gy, DX21UI7Seg::Glyph g, bool inverted = false);
    void DrawBigNumber(int x, int y, int value, int minDigits = 2);
    void DrawBigString(int x, int y, const char* s);
    void DrawCursor(int x, int y, int w, int h);
    void DrawHLine(int x, int y, int w);
    void DrawVLine(int x, int y, int h);

    CSPIMaster*        m_pSPI;
    Config             m_Config;
    CSSD1305SPIDisplay* m_pDisplay;

    // Display state.
    DX21UI::Mode m_Mode;
    int          m_ParamIdx;
    int          m_Value;
    const char*  m_ValueStr;
    const char*  m_VoiceName;
    int          m_VoiceNum;
    const char*  m_Status;
    bool         m_bCompare;
    bool         m_bMemProt;
    bool         m_bDirty;

    // Page-row scratch buffer (one page = 128 bytes).
    u8           m_PageBuf[128];
};

#endif // _display_dx21_h
