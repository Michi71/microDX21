//
// display_dx21.h
//
// CDX21Display - thin wrapper around CSSD1305SPIDisplay (128x32 monochrome)
// that renders the 6 DX21 modes (PLAY/EDIT/PERFORMANCE/FUNCTION/MEMORY)
// using the original ROM's display layout from
// src/opm/firmware/dx21_rom_v1_5.asm.
//
// Layout (128x32 = 4 pages, 8 rows per page):
//
//   +--------------------------------------------------+
//   | <MODE TITLE 16 chars>     (page 0,  rows 0-7)   |  <- mode title
//   | <VALUE 8x16 big digit>     (page 0+1, glyph 16)  |
//   | <PARAM NAME 16 chars>     (page 2,  rows 16-23) |  <- parameter name
//   | <STATUS 16 chars>         (page 3,  rows 24-31) |  <- status line
//   +--------------------------------------------------+
//
// Top 16 rows: 16 chars of small text (mode title) using font6x8 OR
//              a 7-segment digit/value using dx21_ui_7seg.h.
//
// Bottom 16 rows: 16 chars of small text (parameter name and status).
//
// The display reads live parameter values from a COPMEmuAdapter (set via
// SetAdapter after CMicroDX21 is constructed in the kernel). All getters on
// COPMEmuAdapter are thread-safe to call from the display thread; the
// audio thread only writes through SPSC queues and the SysEx deferred
// worker, never through the adapter's public surface.
//

#ifndef _display_dx21_h
#define _display_dx21_h

#include <ssd1305spidisplay.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <circle/types.h>
#include <circle/timer.h>

#include "../opm/dx21_ui_strings.h"
#include "../opm/dx21_ui_7seg.h"

class COPMEmuAdapter;

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

    // ───────────────────────────────────────────────
    // State setters
    // ───────────────────────────────────────────────
    void SetMode(DX21UI::Mode mode)         { m_Mode = mode; MarkDirty(); }
    DX21UI::Mode GetMode() const            { return m_Mode; }

    void SetParamIndex(int idx)             { m_ParamIdx = idx; MarkDirty(); }
    int  GetParamIndex() const              { return m_ParamIdx; }

    void SetValue(int v)                    { m_Value = v; MarkDirty(); }
    int  GetValue() const                   { return m_Value; }

    void SetValueString(const char* s)      { m_ValueStr = s; MarkDirty(); }
    const char* GetValueString() const      { return m_ValueStr; }

    void SetVoiceName(const char* name)     { m_VoiceName = name; MarkDirty(); }
    const char* GetVoiceName() const        { return m_VoiceName; }

    void SetVoiceNumber(int n)              { m_VoiceNum = n; MarkDirty(); }
    int  GetVoiceNumber() const             { return m_VoiceNum; }

    void SetStatus(const char* s)           { m_Status = s; MarkDirty(); }
    void ClearStatus()                      { m_Status = nullptr; MarkDirty(); }

    void SetCompare(bool on)                { m_bCompare = on; MarkDirty(); }
    bool GetCompare() const                 { return m_bCompare; }

    void SetMemoryProtect(bool on)          { m_bMemProt = on; MarkDirty(); }
    bool GetMemoryProtect() const           { return m_bMemProt; }

    // ───────────────────────────────────────────────
    // Synth binding
    // ───────────────────────────────────────────────
    // Once a COPMEmuAdapter is bound, Render() reads live parameter
    // values and the current voice name from the synth. Until bound
    // (or if the adapter is destroyed), Render() falls back to the
    // m_VoiceName / m_Value setters above.
    void SetAdapter(COPMEmuAdapter* pAdapter);
    COPMEmuAdapter* GetAdapter() const      { return m_pAdapter; }

    // ───────────────────────────────────────────────
    // Rendering
    // ───────────────────────────────────────────────
    void Render();

    // Force a re-render if the last one is older than maxAgeMs.
    // Cheap (~ns comparison) and lets MIDI-driven state changes
    // become visible without explicit Set*() calls.
    void InvalidateIfStale(unsigned maxAgeMs);

    // Mark the framebuffer dirty. Caller should ensure the synth
    // has changed before calling this, or use InvalidateIfStale().
    void Invalidate()                       { m_bDirty = true; }

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

    // Mode-specific render helpers. Each writes to the framebuffer
    // for the four 8-row pages.
    void RenderPlayMode();
    void RenderEditMode();
    void RenderPerformanceMode();
    void RenderFunctionMode();
    void RenderMemoryMode();

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

    COPMEmuAdapter* m_pAdapter;

    // m_LastRenderMs tracks wall time of the last successful Render().
    // Render() re-reads the synth values each time it redraws, so the
    // only "stale" data is when nothing causes a Set*() call. For
    // MIDI-driven state changes (Program Change, CC#0 Bank Select)
    // to be visible without explicit Set*() calls, RunCore2() calls
    // InvalidateIfStale(200) every 200ms.
    unsigned    m_LastRenderMs;

    u8           m_PageBuf[128];
};

#endif // _display_dx21_h
