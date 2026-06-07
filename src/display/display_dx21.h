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

    // ───────────────────────────────────────────────
    // Encoder action API
    // ───────────────────────────────────────────────
    //
    // SelectParam() moves the cursor through the per-mode parameter
    // list (PLAY: voice 0..127, EDIT: 0..EDIT_PARAM_COUNT-1, ...).
    // Wraps at both ends. Does NOT write to the synth — it's a pure
    // UI cursor move.
    //
    // AdjustValue() does the meaningful change for the current mode:
    //   PLAY         : 1..128 voice select (kParamInstrument)
    //   EDIT         : value of EDIT_PARAM_NAMES[m_ParamIdx] ±delta
    //   FUNCTION     : value of FUNCTION_NAMES[m_ParamIdx]   ±delta
    //   PERFORMANCE  : 0..127 program select
    //   MEMORY       : 0..TAPE_LABEL_COUNT-1 tape dialog
    //
    // delta is typically ±1 per encoder detent. The synth write
    // happens through the bound COPMEmuAdapter (if any). For modes
    // without a live binding, the integer m_Value is updated and
    // the renderer shows it.
    //
    // Returns the new absolute value (or -1 if not applicable / no
    // adapter). Callers can use this to drive a status message.
    void SelectParam(int delta);
    int  AdjustValue(int delta);

    // Total number of selectable items for the current mode. Used
    // by the encoder for the wrap-around math.
    int  GetParamCountForMode() const;

    // ───────────────────────────────────────────────
    // Memory-mode state machine
    // ───────────────────────────────────────────────
    //
    // The MEMORY screen is a 3-stage dialog:
    //   1. Pick action  : Save / Load / Verify  (m_MemoryAction)
    //   2. Confirm      : YES / NO             (m_MemoryYesNo)
    //   3. Pick group   : 1..16                 (m_MemoryGroup 0..15)
    // After YES, MemoryExecute() runs the chosen action against
    // m_MemoryGroup and stores a 1-shot status string.
    //
    // The encoder and click in MEMORY mode dispatch to these methods.
    // Rotation in stage 1 cycles the action. In stage 2 it toggles
    // YES/NO. In stage 3 it picks the group. Click in stage 1
    // advances to stage 2; click in stage 2 with YES runs the
    // action and shows a 2 s "OK / FAILED" status; click in stage 2
    // with NO or click in stage 3 with the same group jumps back
    // to stage 1.
    void MemoryPickAction(int delta);     // stage 1: rotate Save/Load/Verify
    void MemoryToggleYesNo();            // stage 2: YES <-> NO
    void MemoryPickGroup(int delta);      // stage 3: rotate group 0..15
    void MemoryConfirm();                // click handler: advance or run

    // Result of the most recent MemoryExecute() — shown for ~2 s in
    // the status line. -1 means no recent result. Cleared by
    // SetStatus(nullptr) or by another MemoryConfirm.
    void ClearMemoryResult()            { m_MemoryResult = -1; m_MemoryResultMs = 0; MarkDirty(); }
    int  GetMemoryResult() const        { return m_MemoryResult; }

    // Stage inspection (read-only). 0=pick, 1=confirm, 2=group, 3=result-shown.
    int  GetMemoryStage() const         { return m_MemoryStage; }
    int  GetMemoryAction() const        { return m_MemoryAction; }
    int  GetMemoryYesNo() const         { return m_MemoryYesNo; }
    int  GetMemoryGroup() const         { return m_MemoryGroup; }

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

    // Boot-time splash overlay. When set, Render() ignores m_Mode
    // and shows the boot logo. Cleared by SetSplash(false) when the
    // kernel switches to the normal mode dispatch. Render() honors
    // m_bSplash *before* the m_Mode switch, so any pending mode
    // change becomes visible the moment splash ends.
    void SetSplash(bool on);                // also resets m_SplashProgress
    bool GetSplash() const                  { return m_bSplash; }

    // ───────────────────────────────────────────────
    // Splash fade-in progress
    // ───────────────────────────────────────────────
    //
    // The 4-page splash banner doesn't pop in all at once — it fills
    // in from the top. m_SplashProgress is 0..4 (inclusive):
    //   0 = nothing visible (all pages blank)
    //   1 = page 0 only ("*  YAMAHA  *")
    //   2 = pages 0..1 (adds the 7-seg "DX21" mark)
    //   3 = pages 0..2 (adds "* SYNTHESIZER *")
    //   4 = pages 0..3 (full banner, "v0.1.0  INIT..." appears)
    //
    // SetSplashProgress() is idempotent and MarkDirty()s on change,
    // so Render() will redraw at the next 5 Hz tick. kernel.cpp drives
    // the progress: 4 steps × 250 ms = 1 s fade-in, then a 1 s hold
    // at progress=4, then SetSplash(false) to hand off to PLAY mode.
    void SetSplashProgress(int n)          { if (n < 0) n = 0; if (n > 4) n = 4;
                                              if (n != m_SplashProgress) {
                                                  m_SplashProgress = n;
                                                  MarkDirty();
                                              } }
    int  GetSplashProgress() const         { return m_SplashProgress; }

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
    void RenderSplashMode();

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

    // m_bSplash: while true, Render() ignores m_Mode and shows the
    // boot logo. SetSplash(true) at the end of CDX21Display::Initialize
    // for 2 seconds, then SetSplash(false) + SetMode(kModePlay).
    bool         m_bSplash;

    // Splash fade-in: 0..4. See SetSplashProgress(). Reset to 0 by
    // SetSplash(false) so a re-entry into splash (e.g. after a panic)
    // starts from the top.
    int          m_SplashProgress;

    // Memory-mode state machine. Initial values match the legacy
    // "rotation = next param" behaviour: stage=0 (pick), action=0
    // (save), YesNo=0 (NO), group=0 (1).
    int          m_MemoryStage;     // 0=pick 1=confirm 2=group 3=result
    int          m_MemoryAction;    // 0=save 1=load 2=verify
    int          m_MemoryYesNo;     // 0=NO 1=YES
    int          m_MemoryGroup;     // 0..15 (display 1..16)
    int          m_MemoryResult;    // -1=none, else 0=OK or error code
    unsigned     m_MemoryResultMs;  // wall-time when result was set

    u8           m_PageBuf[128];
};

#endif // _display_dx21_h
