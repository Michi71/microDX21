//
// dx21_input.cpp
//
// CDX21Input: thin CKY040 -> CDX21Display adapter. Maps the encoder's
// TEvent stream onto the 6 DX21 modes (PLAY/EDIT/PERFORMANCE/FUNCTION/
// MEMORY) and the per-mode parameter cursor.
//

#include "dx21_input.h"
#include "display_dx21.h"
#include "opm/dx21_ui_strings.h"
#include <circle/logger.h>
#include <circle/timer.h>

using namespace DX21UI;

static const char FromInput[] = "dx21inp";

CDX21Input* CDX21Input::s_pInstance = nullptr;

CDX21Input::CDX21Input(CDX21Display* pDisplay, CGPIOManager* pGPIO,
                       const Config& cfg)
    : m_pDisplay(pDisplay)
    , m_pGPIO(pGPIO)
    , m_Config(cfg)
    , m_pEncoder(nullptr)
    , m_nHoldCount(0)
    , m_LastClickTicks(0)
    , m_bWaitingDouble(false)
{
}

CDX21Input::~CDX21Input() {
    delete m_pEncoder;
}

bool CDX21Input::Initialize() {
    s_pInstance = this;
    m_pEncoder = new CKY040(m_Config.nPinClk, m_Config.nPinDt, m_Config.nPinSw,
                            m_pGPIO, m_Config.nDetents);
    if (!m_pEncoder) {
        CLogger::Get()->Write(FromInput, LogError, "alloc failed");
        return false;
    }
    if (!m_pEncoder->Initialize()) {
        CLogger::Get()->Write(FromInput, LogError, "CKY040 init failed");
        delete m_pEncoder;
        m_pEncoder = nullptr;
        s_pInstance = nullptr;
        return false;
    }
    m_pEncoder->RegisterEventHandler(StaticEventHandler, this);
    CLogger::Get()->Write(FromInput, LogNotice,
        "KY-040 ready (CLK=%u DT=%u SW=%u mode=%s detents=%u)",
        m_Config.nPinClk, m_Config.nPinDt, m_Config.nPinSw,
        m_pGPIO ? "ISR" : "poll", m_Config.nDetents);
    return true;
}

void CDX21Input::StaticEventHandler(CKY040::TEvent ev, void* pParam) {
    CDX21Input* self = static_cast<CDX21Input*>(pParam);
    if (self) self->OnEvent(ev);
}

void CDX21Input::OnEvent(CKY040::TEvent ev) {
    ApplyEvent(ev);
}

// ────────────────────────────────────────────────────────────────────
// Event -> Display action mapping. This is the entire "UI state machine":
//
//   One rotary encoder + a click button. No menus, no sub-screens, no
//   touch. The mapping mirrors what the original DX21 firmware does in
//   the rtn_85 / rtn_118 / rtn_184 switch statements in the ROM.
//
//   - Rotate CW  : next parameter / value (delta = +1)
//   - Rotate CCW : previous (delta = -1)
//   - Single click: cycle to next mode (PLAY -> EDIT -> PERFORM -> FUNCTION -> MEMORY -> PLAY)
//   - Double click: toggle COMPARE overlay (in PLAY/EDIT/PERFORMANCE)
//   - Long press (hold): toggle MEMORY PROTECT indicator
// ────────────────────────────────────────────────────────────────────
void CDX21Input::ApplyEvent(CKY040::TEvent ev) {
    if (!m_pDisplay) return;

    switch (ev) {
        case CKY040::EventClockwise: {
            int idx = m_pDisplay->GetParamIndex() + 1;
            // Clamp to the per-mode param count.
            Mode mode = m_pDisplay->GetMode();
            int maxIdx = 0;
            switch (mode) {
                case kModeEdit:        maxIdx = EDIT_PARAM_COUNT - 1; break;
                case kModePerformance: maxIdx = PLAY_LABEL_COUNT - 1;  break;
                case kModeFunction:    maxIdx = FUNCTION_COUNT - 1;    break;
                case kModeMemory:      maxIdx = TAPE_LABEL_COUNT - 1;  break;
                default:               maxIdx = 127;                   break; // PLAY: voice 1..128
            }
            if (idx > maxIdx) idx = 0;  // wrap
            m_pDisplay->SetParamIndex(idx);
            break;
        }
        case CKY040::EventCounterclockwise: {
            int idx = m_pDisplay->GetParamIndex() - 1;
            Mode mode = m_pDisplay->GetMode();
            int maxIdx = 0;
            switch (mode) {
                case kModeEdit:        maxIdx = EDIT_PARAM_COUNT - 1; break;
                case kModePerformance: maxIdx = PLAY_LABEL_COUNT - 1;  break;
                case kModeFunction:    maxIdx = FUNCTION_COUNT - 1;    break;
                case kModeMemory:      maxIdx = TAPE_LABEL_COUNT - 1;  break;
                default:               maxIdx = 127;                   break;
            }
            if (idx < 0) idx = maxIdx;  // wrap
            m_pDisplay->SetParamIndex(idx);
            break;
        }

        case CKY040::EventSwitchClick: {
            // Short click: cycle to next mode. The original DX21's
            // PLAY/EDIT/FUNCTION/COMPARE buttons are mapped to a single
            // "mode" encoder in microDX21 (we have one encoder, not 5
            // buttons).
            Mode mode = m_pDisplay->GetMode();
            switch (mode) {
                case kModePlay:        m_pDisplay->SetMode(kModeEdit);        break;
                case kModeEdit:        m_pDisplay->SetMode(kModePerformance); break;
                case kModePerformance: m_pDisplay->SetMode(kModeFunction);    break;
                case kModeFunction:    m_pDisplay->SetMode(kModeMemory);      break;
                case kModeMemory:      m_pDisplay->SetMode(kModePlay);        break;
                default:               m_pDisplay->SetMode(kModePlay);        break;
            }
            m_pDisplay->SetParamIndex(0);
            m_pDisplay->ClearStatus();
            break;
        }

        case CKY040::EventSwitchDoubleClick: {
            // Double-click: COMPARE toggle (only meaningful in
            // PLAY/EDIT/PERFORMANCE).
            Mode mode = m_pDisplay->GetMode();
            if (mode == kModePlay || mode == kModeEdit || mode == kModePerformance) {
                m_pDisplay->SetCompare(!m_pDisplay->GetCompare());
            }
            break;
        }

        case CKY040::EventSwitchHold: {
            // Each second the switch is held: toggle memory-protect
            // indicator on second tick (so a single long press doesn't
            // immediately fire).
            m_nHoldCount++;
            if (m_nHoldCount == 2) {
                m_pDisplay->SetMemoryProtect(!m_pDisplay->GetMemoryProtect());
                m_pDisplay->SetStatus(m_pDisplay->GetMemoryProtect()
                                      ? "MEMORY PROTECTED"
                                      : "MEMORY UNPROTECTED");
                m_nHoldCount = 0;
            }
            break;
        }

        case CKY040::EventSwitchUp:
        case CKY040::EventSwitchDown:
        case CKY040::EventSwitchTripleClick:
        case CKY040::EventUnknown:
        default:
            break;
    }
}
