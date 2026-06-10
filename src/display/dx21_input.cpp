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
#include "audio/opmemuadapter.h"   // full type: we call pA->setMemoryProtect()
#include <cstdio>
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
    , m_bBrowse(true)  // start in "browse" — rotation picks the next param
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

    // Per-mode semantics for a single encoder detent. Resolved once
    // per event so the CW/CCW arms stay symmetric.
    //
    //   PLAY         : rotation selects the next/prev voice (1..128)
    //                  AND calls m_pAdapter->setCurrentProgram() —
    //                  i.e. the user hears the change immediately.
    //   EDIT         : rotation edits the *value* of the currently
    //                  selected EDIT_PARAM_NAMES entry. To switch
    //                  parameters the user has to single-click (which
    //                  cycles the mode and comes back via the new
    //                  mode's default) — for now we keep the legacy
    //                  "rotation = next param" behaviour for EDIT
    //                  and add a separate "double-click = edit value"
    //                  gesture. Wait — see below: per agreement with
    //                  the user we make rotation always = "value
    //                  change of the current param" for EDIT and
    //                  FUNCTION, because the param index is mutated
    //                  by the mode-cycling single-click.
    //   FUNCTION     : same as EDIT.
    //   PERFORMANCE  : rotation = voice select (program change).
    //   MEMORY       : rotation = scroll the tape dialog list (no
    //                  synth write; cursor only).
    //
    // To keep the EDIT/FUNCTION experience navigable with a single
    // encoder, the model is:
    //   - Single click on the encoder cycles the mode and resets
    //     m_ParamIdx to 0 (existing behaviour).
    //   - Rotation in PLAY/PERFORMANCE/MEMORY navigates the list.
    //   - Rotation in EDIT/FUNCTION edits the value of the current
    //     param. To "browse" EDIT entries, the user has to use a
    //     dedicated gesture — but we only have one encoder, so we
    //     map long-press (≥2 ticks) to "enter browse mode for
    //     EDIT/FUNCTION". See the EventSwitchHold arm below.
    //
    // Implementation: rotation arms into either SelectParam (browse
    // the list) or AdjustValue (edit the value). The mode + a
    // bBrowse flag decide which one.

    switch (ev) {
        case CKY040::EventClockwise: {
            if (m_pDisplay->GetMode() == DX21UI::kModeMemory) {
                // MEMORY-mode state machine dispatch. The encoder
                // drives the picker of the current stage; click
                // advances. The m_bBrowse flag is ignored here because
                // MEMORY is a dialog, not a value editor.
                int stage = m_pDisplay->GetMemoryStage();
                if (stage == 0)      m_pDisplay->MemoryPickAction(+1);
                else if (stage == 1) m_pDisplay->MemoryToggleYesNo();
                else if (stage == 2) m_pDisplay->MemoryPickGroup(+1);
                // stage 3 ignores rotation; the timer auto-clears.
            } else if (m_bBrowse) {
                m_pDisplay->SelectParam(+1);
            } else {
                int v = m_pDisplay->AdjustValue(+1);
                if (v >= 0) {
                    char msg[24];
                    snprintf(msg, sizeof(msg), "VAL=%3d", v);
                    m_pDisplay->SetStatus(msg);
                }
            }
            break;
        }
        case CKY040::EventCounterclockwise: {
            if (m_pDisplay->GetMode() == DX21UI::kModeMemory) {
                int stage = m_pDisplay->GetMemoryStage();
                if (stage == 0)      m_pDisplay->MemoryPickAction(-1);
                else if (stage == 1) m_pDisplay->MemoryToggleYesNo();
                else if (stage == 2) m_pDisplay->MemoryPickGroup(-1);
            } else if (m_bBrowse) {
                m_pDisplay->SelectParam(-1);
            } else {
                int v = m_pDisplay->AdjustValue(-1);
                if (v >= 0) {
                    char msg[24];
                    snprintf(msg, sizeof(msg), "VAL=%3d", v);
                    m_pDisplay->SetStatus(msg);
                }
            }
            break;
        }

        case CKY040::EventSwitchClick: {
            // In MEMORY mode, the click is the dialog "OK" button:
            // advance the state machine. Otherwise, cycle the mode.
            if (m_pDisplay->GetMode() == DX21UI::kModeMemory) {
                m_pDisplay->MemoryConfirm();
                break;
            }
            // In FUNCTION mode, the click triggers action entries
            // (Recall / Init / Bulk Transmit) when the user is on a
            // -1 entry. Otherwise it just cycles to the next mode
            // (existing behaviour).
            if (m_pDisplay->GetMode() == DX21UI::kModeFunction) {
                if (m_pDisplay->TriggerFunctionAction()) {
                    break;
                }
            }
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
            // Each second the switch is held. m_nHoldCount=1 fires
            // on the first tick (~1 s), m_nHoldCount=2 on the next.
            //
            // First tick (1 s): toggle EDIT-mode browse flag. When
            // m_bBrowse is true, rotation navigates the param list;
            // when false, rotation edits the value of the current
            // param. This is the only way we have to switch between
            // "browse" and "edit" with a single encoder and click
            // (single click is reserved for mode cycling).
            //
            // Second tick (2 s): toggle memory-protect. Held for two
            // full seconds to avoid accidental activation.
            m_nHoldCount++;
            if (m_nHoldCount == 1) {
                m_bBrowse = !m_bBrowse;
                m_pDisplay->SetStatus(m_bBrowse ? "BROWSE"
                                                : "EDIT");
            } else if (m_nHoldCount == 2) {
                bool newProt = !m_pDisplay->GetMemoryProtect();
                m_pDisplay->SetMemoryProtect(newProt);
                // Forward into the synth so the gates in COPMEmu /
                // CDX21Memory take effect. The adapter is bound by
                // the kernel after both are constructed.
                if (COPMEmuAdapter* pA = m_pDisplay->GetAdapter()) {
                    pA->setMemoryProtect(newProt);
                }
                m_pDisplay->SetStatus(newProt
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
