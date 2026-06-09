//
// dx21_input.h
//
// CDX21Input - Rotary encoder adapter.  Owns a CKY040 (from Circle's
// sensor addon, libs/.../addon/sensor/ky040.h) and translates its
// TEvent stream into Set*() calls on a CDX21Display*.
//
// Why CKY040 and not a custom ISR?
//   CKY040 is the canonical Circle driver for the KY-040 module. It runs
//   in INTERRUPT mode (encoder pins) and a small kernel-timer for switch
//   debounce/hold. The driver handles:
//     - Gray-code decoding with a 16-entry lookup table
//     - Bounce rejection via configurable detent count (1=quarter step,
//       2=half, 4=full step)
//     - Switch debounce (50 ms) and click / double-click / triple-click
//       / hold state machine
//   We just receive TEvent callbacks and forward them to the display.
//

#ifndef _dx21_input_h
#define _dx21_input_h

#include <ky040.h>
#include <circle/gpiomanager.h>
#include <circle/types.h>

class CDX21Display;

class CDX21Input {
public:
    // Single-encoder config. BCM pin numbers (Brcm).
    struct Config {
        unsigned nPinClk   = 10;   // Encoder pin A (CLK)
        unsigned nPinDt    = 9;    // Encoder pin B (DT)
        unsigned nPinSw    = 11;   // Encoder switch
        unsigned nDetents  = 4;    // 1=quarter, 2=half, 4=full step
    };

    CDX21Input(CDX21Display* pDisplay, CGPIOManager* pGPIO,
               const Config& cfg);
    ~CDX21Input();

    bool Initialize();

    // For polling mode (if pGPIO was nullptr in constructor).
    void Update() { if (m_pEncoder) m_pEncoder->Update(); }

private:
    // The CKY040 event handler.  Bound statically; we route via s_pInstance.
    void OnEvent(CKY040::TEvent ev);
    static void StaticEventHandler(CKY040::TEvent ev, void* pParam);

    // The dispatch from a TEvent to a state-machine action on the display.
    void ApplyEvent(CKY040::TEvent ev);

private:
    CDX21Display*   m_pDisplay;
    CGPIOManager*   m_pGPIO;
    Config          m_Config;
    CKY040*         m_pEncoder;

    // State for short vs long press / debouncing of "next mode".
    unsigned        m_nHoldCount;
    unsigned        m_LastClickTicks;
    bool            m_bWaitingDouble;

    // Browse-vs-edit flag for EDIT/FUNCTION modes. true = rotation
    // navigates the param list (default after mode-cycle); false =
    // rotation edits the value of the current param. Toggled by the
    // first tick of EventSwitchHold.
    bool            m_bBrowse;

    static CDX21Input* s_pInstance;
};

#endif
