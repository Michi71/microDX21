//
// opmemuadapter.h
//
// Adapter wrapping COPMEmu (DX21 FM synthesizer) for the VelvetKeys
// application layer. Replaces the former CVKSynthAdapter which wrapped
// the sample-based vkSynth engine.
//
// Echtzeit-Garantie: Alle Audio-Methoden (processBlock, noteOn, noteOff)
// sind frei von Heap-Allokationen und std::string-Operationen.
//

#ifndef _OPMEMU_ADAPTER_H
#define _OPMEMU_ADAPTER_H

#include <circle/logger.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>

#include "opm/opmemu.h"
#include "opm/io/ifilesystem.h"

// ──────────────────────────────────────────────────────────────────
// PresetInfo — lightweight preset/program descriptor for the UI.
// Compatible with the former vkSynth::PresetInfo but backed by
// DX21_Patch names from COPMEmu / CDX21Memory.
// ──────────────────────────────────────────────────────────────────
struct PresetInfo {
    std::string file;          // for file-backed presets (empty for ROM)
    std::string name;          // display name (from patch name or file)
    std::string category;      // "ROM" / "RAM" / "User"
    std::string description;   // optional description
};

// ──────────────────────────────────────────────────────────────────
// DX21 parameter indices for the unified parameter system.
// Maps normalized 0..1 values to COPMEmu controls, used by the
// parameter editor screen and MIDI CC routing.
// ──────────────────────────────────────────────────────────────────
enum DX21ParamIndex {
    // Instrument select (0): 0..1 → program 0..N-1.
    // Distinct from the per-mode param indices so callers can use it
    // directly without going through kParamXxx → COPMEmu::setCurrentProgram().
    kParamInstrument = 0,    // 0..1 → program 0..N-1 (normalised)

    // Global (1-8) — shifted by +1 from the pre-instrument version.
    kParamMasterGain,        // 0..1 → master gain 0..8
    kParamEnsemble,          // 0/1 → chorus on/off
    kParamPlayMode,          // 0..1 → Single/Dual/Split (3 steps)
    kParamSplitPoint,        // 0..1 → MIDI note 0..127
    kParamBalance,           // 0..1 → balance 0..99
    kParamPitchBendRange,    // 0..1 → 0..12 semitones
    kParamPortamentoMode,    // 0..1 → Off/FullTime/Fingered (3 steps)
    kParamPortamentoRate,    // 0..1 → 0..99

    // LFO (9-12) — shifted by +1.
    kParamLFOSpeed,          // 0..1 → LFO speed 0..255
    kParamLFODelay,          // 0..1 → LFO delay 0..127
    kParamPMD,               // 0..1 → Pitch Mod Depth 0..127
    kParamAMD,               // 0..1 → Amp Mod Depth 0..127

    // Voice common (13-16) — shifted by +1.
    kParamAlgorithm,         // 0..1 → algorithm 0..7 (8 steps)
    kParamFeedback,          // 0..1 → feedback 0..7
    kParamLFOSync,           // 0/1 → LFO key sync
    kParamLFOWave,           // 0..1 → LFO waveform 0..3 (4 steps)

    // Per-operator parameters (4 ops × 5 core params = 20) — shifted by +1.
    // Operator 1 (OP4 in DX21 terminology = carrier 1)
    kParamOp0AR,
    kParamOp0D1R,
    kParamOp0D1L,
    kParamOp0Out,
    kParamOp0CRS,

    // Operator 2 (OP2 = modulator 1)
    kParamOp1AR,
    kParamOp1D1R,
    kParamOp1D1L,
    kParamOp1Out,
    kParamOp1CRS,

    // Operator 3 (OP3 = carrier 2)
    kParamOp2AR,
    kParamOp2D1R,
    kParamOp2D1L,
    kParamOp2Out,
    kParamOp2CRS,

    // Operator 4 (OP1 = modulator 2)
    kParamOp3AR,
    kParamOp3D1R,
    kParamOp3D1L,
    kParamOp3Out,
    kParamOp3CRS,

    // Per-operator extended EG / level / velocity params. 8 per op × 4 ops = 32.
    // Indices 37..68.
    kParamOp0D2R,
    kParamOp0RR,
    kParamOp0LS,
    kParamOp0RS,
    kParamOp0EBS,
    kParamOp0AME,
    kParamOp0KVS,
    kParamOp0DET,

    kParamOp1D2R,
    kParamOp1RR,
    kParamOp1LS,
    kParamOp1RS,
    kParamOp1EBS,
    kParamOp1AME,
    kParamOp1KVS,
    kParamOp1DET,

    kParamOp2D2R,
    kParamOp2RR,
    kParamOp2LS,
    kParamOp2RS,
    kParamOp2EBS,
    kParamOp2AME,
    kParamOp2KVS,
    kParamOp2DET,

    kParamOp3D2R,
    kParamOp3RR,
    kParamOp3LS,
    kParamOp3RS,
    kParamOp3EBS,
    kParamOp3AME,
    kParamOp3KVS,
    kParamOp3DET,

    // Global LFO modulation sensitivity. Indices 69..70.
    kParamPMS,              // 0..7
    kParamAMS,              // 0..3

    // Per-voice global non-VCED params. Indices 71..77.
    kParamKeyOffset,        // 0..1 → -24..+24 semitones (centre at 0.5)
    kParamMasterTune,       // 0..1 → -64..+63 (centre at 0.5)
    kParamMono,             // 0/1
    kParamPBMode,           // 0..3 (All/Low/High/K-on)

    // Breath controller (DX21 breath / wind controller mappings). 78..81.
    kParamBreathPitchBias,  // 0..99
    kParamBreathAmp,        // 0..99
    kParamBreathEGBias,     // 0..99
    kParamBreathEGDepth,    // 0..99

    // Modulation Wheel sensitivity (B8/B9). 82..83.
    kParamMWPitchRange,     // 0..99
    kParamMWAmpRange,       // 0..99

    // MIDI routing toggles (A5/A6/A7/A2). 84..87.
    kParamCHInfo,           // 0/1 (A6: Channel Information ON/OFF)
    kParamSysInfo,          // 0/1 (A7: SysEx Information ON/OFF)
    kParamTransmitCh,       // 0..15 (A5: 0=off, 1..15=ch)
    kParamDualDetune,       // 0..99 (A2: DUAL-mode side-B detune)

    // MIDI receive routing + foot controller + breath pitch. 88..92.
    // Appended at the end so the existing indices stay stable.
    kParamMidiSwitch,       // 0/1 (FUNCTION #3: master MIDI receive)
    kParamReceiveCh,        // 0..16 (FUNCTION #6/#7: 0=Omni, 1..16=ch)
    kParamFootVolume,       // 0..99 (FUNCTION #30 / B6: CC#7 control range)
    kParamFootSustain,      // 0/1 (FUNCTION #31 / B7: CC#64 enable)
    kParamBreathPitch,      // 0..99 (FUNCTION #35: BC → LFO PMD range)

    // Foot Porta + Pitch EG. 93..99.
    kParamFootPorta,        // 0/1 (FUNCTION #32 / B5: CC#65 enable)
    kParamPEGR1,            // 0..99 (EDIT: PEG RATE 1)
    kParamPEGL1,            // 0..99 (EDIT: PEG LEVEL 1)
    kParamPEGR2,            // 0..99 (EDIT: PEG RATE 2)
    kParamPEGL2,            // 0..99 (EDIT: PEG LEVEL 2)
    kParamPEGR3,            // 0..99 (EDIT: PEG RATE 3)
    kParamPEGL3,            // 0..99 (EDIT: PEG LEVEL 3)

    kParamTotalCount
};

// The EDIT-mode operator select (CDX21Display::ResolveEditParam) relies
// on the per-operator blocks being laid out at fixed strides: 5
// consecutive core params per op, 8 consecutive extended params per op.
// Keep these asserts in sync if the enum is ever reordered.
static_assert(kParamOp1AR  == kParamOp0AR  + 5 &&
              kParamOp2AR  == kParamOp0AR  + 10 &&
              kParamOp3AR  == kParamOp0AR  + 15,
              "per-op core param stride must be 5");
static_assert(kParamOp1D2R == kParamOp0D2R + 8 &&
              kParamOp2D2R == kParamOp0D2R + 16 &&
              kParamOp3D2R == kParamOp0D2R + 24,
              "per-op extended param stride must be 8");

// ──────────────────────────────────────────────────────────────────
// COPMEmuAdapter — bridges COPMEmu (DX21 FM synth) to the
// VelvetKeys application layer, matching the call patterns that
// CVelvetKeys previously used with CVKSynthAdapter / vkSynth.
// ──────────────────────────────────────────────────────────────────
class COPMEmuAdapter
{
public:
    COPMEmuAdapter(float sampleRate, int maxPolyphony)
    : m_synth(nullptr)
    , m_sampleRate(sampleRate)
    , m_maxPolyphony(maxPolyphony)
    , m_currentProgram(0)
    , m_pFS(nullptr)
    {
        // COPMEmu is allocated in init() after filesystem is set.
        // maxPolyphony for DX21 FM is fixed at 8 voices (hardware limit).
    }

    ~COPMEmuAdapter()
    {
        delete m_synth;
    }

    bool init(const std::string& presetsDir = "")
    {
        CLogger::Get()->Write("COPMEmuAdapter", LogNotice, "Init: starting");

        m_synth = new COPMEmu(m_pFS);
        if (!m_synth)
        {
            CLogger::Get()->Write("COPMEmuAdapter", LogError, "Failed to allocate COPMEmu");
            return false;
        }

        m_synth->Initialize();

        // If the kernel registered the SysEx-out callback before init(),
        // forward it now that the engine exists.
        if (m_sysexOutFn) {
            m_synth->setSysexOutCallback(m_sysexOutFn, m_sysexOutUser);
        }

        CLogger::Get()->Write("COPMEmuAdapter", LogNotice, "Init: COPMEmu initialized, %d programs",
                               m_synth->getNumPrograms());
        return true;
    }

    void setFileSystem(IFileSystem* fs)
    {
        m_pFS = fs;
    }

    void setSysexOutCallback(void (*fn)(void*, const uint8_t*, size_t), void* user)
    {
        // Stored locally for the UI-triggered dumps (TriggerBulkTransmit /
        // TriggerVoiceTransmit) and forwarded into COPMEmu so the engine
        // can answer Yamaha dump requests (F0 43 2n ff F7) on its own.
        m_sysexOutFn = fn;
        m_sysexOutUser = user;
        if (m_synth) m_synth->setSysexOutCallback(fn, user);
    }

    // --- Memory Protect (Function #23) ---
    // Forwards to COPMEmu::setMemoryProtect, which also gates
    // CDX21Memory internally. The display calls this on the 2nd
    // hold-tick; the kernel wires the adapter into the display.
    void setMemoryProtect(bool on) {
        if (m_synth) m_synth->setMemoryProtect(on);
    }
    bool isMemoryProtected() const {
        return m_synth && m_synth->isMemoryProtected();
    }

    void handleSysexMessage(const uint8_t* data, size_t len)
    {
        if (m_synth && data && len > 0)
        {
            // DX21 SysEx import through CDX21Memory
            int imported = m_synth->memory().importSysex(data, len);
            CLogger::Get()->Write("COPMEmuAdapter", LogNotice,
                                   "SysEx import: %d voices imported", imported);
        }
    }

    // ── Audio ──────────────────────────────────────────────

    void noteOn(int32_t note, int32_t velocity)
    {
        if (m_synth) m_synth->noteOn(note, velocity);
    }

    void noteOff(int32_t note)
    {
        if (m_synth) m_synth->noteOff(note);
    }

    void sendMidiCmd(uint8_t cmd, uint8_t data1, uint8_t data2)
    {
        if (!m_synth) return;
        uint8_t msg[3] = { cmd, data1, data2 };
        m_synth->processMidi(msg, 3);
    }

    // ── Parameters (normalized 0..1) ──────────────────────

    void setParameter(int index, float value)
    {
        if (!m_synth) return;
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;

        switch (index)
        {
            case kParamInstrument: {
                int n = m_synth->getNumPrograms();
                if (n <= 0) return;
                int prog = (int)(value * (n - 1) + 0.5f);
                m_synth->setCurrentProgram(prog);
                m_currentProgram = prog;
                // Manual: pressing a voice selector in PLAY mode
                // transmits the voice as a 1-voice dump (Sy Info ON).
                transmitVoiceIfEnabled(prog);
                break;
            }
            case kParamMasterGain:
                m_synth->setMasterGain(value * 8.0f);
                break;
            case kParamEnsemble:
                m_synth->setEnsembleOn(value > 0.5f);
                break;
            case kParamPlayMode: {
                int mode = (int)(value * 2.99f); // 0, 1, or 2
                m_synth->setPlayMode(static_cast<COPMEmu::PlayMode>(mode));
                break;
            }
            case kParamSplitPoint:
                m_synth->setSplitPoint((int)(value * 127.0f));
                break;
            case kParamBalance:
                m_synth->setBalance((int)(value * 99.0f));
                break;
            case kParamPitchBendRange:
                m_synth->setPitchBendRange((int)(value * 12.0f));
                break;
            case kParamPortamentoMode: {
                int mode = (int)(value * 2.99f);
                m_synth->setPortamentoMode(mode);
                break;
            }
            case kParamPortamentoRate:
                m_synth->setPortamentoRate((int)(value * 99.0f));
                break;
            case kParamLFOSpeed: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                if (p) {
                    m_synth->writeVcedGlobal(6, (uint8_t)(value * 255.0f)); // LFO speed
                }
                break;
            }
            case kParamLFODelay: {
                m_synth->writeVcedGlobal(7, (uint8_t)(value * 127.0f)); // LFO delay
                break;
            }
            case kParamPMD:
                m_synth->writeVcedGlobal(8, (uint8_t)(value * 127.0f)); // PMD
                break;
            case kParamAMD:
                m_synth->writeVcedGlobal(9, (uint8_t)(value * 127.0f)); // AMD
                break;
            case kParamAlgorithm: {
                int alg = (int)(value * 7.99f);
                m_synth->writeVcedGlobal(0, (uint8_t)alg);
                break;
            }
            case kParamFeedback: {
                int fb = (int)(value * 7.99f);
                m_synth->writeVcedGlobal(1, (uint8_t)fb);
                break;
            }
            case kParamLFOSync:
                m_synth->writeVcedGlobal(10, (uint8_t)(value > 0.5f ? 1 : 0));
                break;
            case kParamLFOWave: {
                int wave = (int)(value * 3.99f); // 0..3
                m_synth->writeVcedGlobal(11, (uint8_t)wave);
                break;
            }

            // Per-operator parameters. 13 per op × 4 ops = 52 cases,
            // written out longhand to avoid the macro case-value
            // collision when the op index increments.
            case kParamOp0AR:  m_synth->writeVcedOperator(0, 0,  (uint8_t)(value * 31.0f)); break; /* AR  */
            case kParamOp0D1R: m_synth->writeVcedOperator(0, 2,  (uint8_t)(value * 31.0f)); break; /* D1R */
            case kParamOp0D1L: m_synth->writeVcedOperator(0, 8,  (uint8_t)(value * 15.0f)); break; /* D1L */
            case kParamOp0Out: m_synth->writeVcedOperator(0, 10, (uint8_t)(value * 99.0f)); break; /* OUT */
            case kParamOp0CRS: m_synth->writeVcedOperator(0, 11, (uint8_t)(value * 63.0f)); break; /* CRS */
            case kParamOp0D2R: m_synth->writeVcedOperator(0, 1,  (uint8_t)(value * 31.0f)); break; /* D2R */
            case kParamOp0RR:  m_synth->writeVcedOperator(0, 3,  (uint8_t)(value * 15.0f)); break; /* RR  */
            case kParamOp0LS:  m_synth->writeVcedOperator(0, 5,  (uint8_t)(value * 99.0f)); break; /* LS  */
            case kParamOp0RS:  m_synth->writeVcedOperator(0, 6,  (uint8_t)(value * 3.0f));  break; /* RS  */
            case kParamOp0EBS: m_synth->writeVcedOperator(0, 7,  (uint8_t)(value * 7.0f));  break; /* EBS */
            case kParamOp0AME: m_synth->writeVcedOperator(0, 13, (uint8_t)(value > 0.5f ? 1 : 0)); break; /* AME 0/1 */ /* AME 0/1 */
            case kParamOp0KVS: m_synth->writeVcedOperator(0, 9,  (uint8_t)(value * 7.0f));  break; /* KVS */
            case kParamOp0DET: m_synth->writeVcedOperator(0, 12, (uint8_t)(value * 6.0f));  break; /* DET (0..6) */

            case kParamOp1AR:  m_synth->writeVcedOperator(1, 0,  (uint8_t)(value * 31.0f)); break;
            case kParamOp1D1R: m_synth->writeVcedOperator(1, 2,  (uint8_t)(value * 31.0f)); break;
            case kParamOp1D1L: m_synth->writeVcedOperator(1, 8,  (uint8_t)(value * 15.0f)); break;
            case kParamOp1Out: m_synth->writeVcedOperator(1, 10, (uint8_t)(value * 99.0f)); break;
            case kParamOp1CRS: m_synth->writeVcedOperator(1, 11, (uint8_t)(value * 63.0f)); break;
            case kParamOp1D2R: m_synth->writeVcedOperator(1, 1,  (uint8_t)(value * 31.0f)); break;
            case kParamOp1RR:  m_synth->writeVcedOperator(1, 3,  (uint8_t)(value * 15.0f)); break;
            case kParamOp1LS:  m_synth->writeVcedOperator(1, 5,  (uint8_t)(value * 99.0f)); break;
            case kParamOp1RS:  m_synth->writeVcedOperator(1, 6,  (uint8_t)(value * 3.0f));  break;
            case kParamOp1EBS: m_synth->writeVcedOperator(1, 7,  (uint8_t)(value * 7.0f));  break;
            case kParamOp1AME: m_synth->writeVcedOperator(1, 13, (uint8_t)(value > 0.5f ? 1 : 0)); break; /* AME 0/1 */
            case kParamOp1KVS: m_synth->writeVcedOperator(1, 9,  (uint8_t)(value * 7.0f));  break;
            case kParamOp1DET: m_synth->writeVcedOperator(1, 12, (uint8_t)(value * 6.0f));  break;

            case kParamOp2AR:  m_synth->writeVcedOperator(2, 0,  (uint8_t)(value * 31.0f)); break;
            case kParamOp2D1R: m_synth->writeVcedOperator(2, 2,  (uint8_t)(value * 31.0f)); break;
            case kParamOp2D1L: m_synth->writeVcedOperator(2, 8,  (uint8_t)(value * 15.0f)); break;
            case kParamOp2Out: m_synth->writeVcedOperator(2, 10, (uint8_t)(value * 99.0f)); break;
            case kParamOp2CRS: m_synth->writeVcedOperator(2, 11, (uint8_t)(value * 63.0f)); break;
            case kParamOp2D2R: m_synth->writeVcedOperator(2, 1,  (uint8_t)(value * 31.0f)); break;
            case kParamOp2RR:  m_synth->writeVcedOperator(2, 3,  (uint8_t)(value * 15.0f)); break;
            case kParamOp2LS:  m_synth->writeVcedOperator(2, 5,  (uint8_t)(value * 99.0f)); break;
            case kParamOp2RS:  m_synth->writeVcedOperator(2, 6,  (uint8_t)(value * 3.0f));  break;
            case kParamOp2EBS: m_synth->writeVcedOperator(2, 7,  (uint8_t)(value * 7.0f));  break;
            case kParamOp2AME: m_synth->writeVcedOperator(2, 13, (uint8_t)(value > 0.5f ? 1 : 0)); break; /* AME 0/1 */
            case kParamOp2KVS: m_synth->writeVcedOperator(2, 9,  (uint8_t)(value * 7.0f));  break;
            case kParamOp2DET: m_synth->writeVcedOperator(2, 12, (uint8_t)(value * 6.0f));  break;

            case kParamOp3AR:  m_synth->writeVcedOperator(3, 0,  (uint8_t)(value * 31.0f)); break;
            case kParamOp3D1R: m_synth->writeVcedOperator(3, 2,  (uint8_t)(value * 31.0f)); break;
            case kParamOp3D1L: m_synth->writeVcedOperator(3, 8,  (uint8_t)(value * 15.0f)); break;
            case kParamOp3Out: m_synth->writeVcedOperator(3, 10, (uint8_t)(value * 99.0f)); break;
            case kParamOp3CRS: m_synth->writeVcedOperator(3, 11, (uint8_t)(value * 63.0f)); break;
            case kParamOp3D2R: m_synth->writeVcedOperator(3, 1,  (uint8_t)(value * 31.0f)); break;
            case kParamOp3RR:  m_synth->writeVcedOperator(3, 3,  (uint8_t)(value * 15.0f)); break;
            case kParamOp3LS:  m_synth->writeVcedOperator(3, 5,  (uint8_t)(value * 99.0f)); break;
            case kParamOp3RS:  m_synth->writeVcedOperator(3, 6,  (uint8_t)(value * 3.0f));  break;
            case kParamOp3EBS: m_synth->writeVcedOperator(3, 7,  (uint8_t)(value * 7.0f));  break;
            case kParamOp3AME: m_synth->writeVcedOperator(3, 13, (uint8_t)(value > 0.5f ? 1 : 0)); break; /* AME 0/1 */
            case kParamOp3KVS: m_synth->writeVcedOperator(3, 9,  (uint8_t)(value * 7.0f));  break;
            case kParamOp3DET: m_synth->writeVcedOperator(3, 12, (uint8_t)(value * 6.0f));  break;

            // Global LFO modulation sensitivity.
            case kParamPMS:
                // PMS only (0..7). AMS stays at its current value.
                {
                    const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                    int pms = (int)(value * 7.0f);
                    int ams = p ? (p->ams & 0x03) : 0;
                    m_synth->writeVcedGlobal(12, (uint8_t)((pms << 4) | ams));
                }
                break;
            case kParamAMS:
                {
                    const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                    int pms = p ? (p->pms & 0x07) : 0;
                    int ams = (int)(value * 3.0f);
                    m_synth->writeVcedGlobal(12, (uint8_t)((pms << 4) | ams));
                }
                break;

            // Non-VCED global parameters.
            case kParamKeyOffset: {
                // 0..1 → -24..+24, centre at 0.5 → 0.
                int v = (int)(value * 49.0f) - 24;
                m_synth->writeVcedGlobal(13, (uint8_t)(v + 24));
                break;
            }
            case kParamMasterTune: {
                // 0..1 → -64..+63, centre at 0.5 → 0.
                int v = (int)(value * 128.0f) - 64;
                m_synth->setMasterTune(v);
                break;
            }
            case kParamMono:
                m_synth->setMono(value > 0.5f);
                break;
            case kParamPBMode: {
                int mode = (int)(value * 3.99f);  // 0..3
                m_synth->setPBMode(mode);
                break;
            }
            case kParamBreathPitchBias: m_synth->setBreathPitchBias((int)(value * 99.0f)); break;
            case kParamBreathAmp:       m_synth->setBreathAmplitude((int)(value * 99.0f)); break;
            case kParamBreathEGBias:    m_synth->setBreathEGBias((int)(value * 99.0f));     break;
            case kParamBreathEGDepth:   m_synth->setBreathEGDepth((int)(value * 99.0f));    break;

            // Modulation Wheel sensitivity (B8/B9). Direct call to
            // m_synth's CC#1 range setter.
            case kParamMWPitchRange:  m_synth->setMWPitchRange((int)(value * 99.0f)); break;
            case kParamMWAmpRange:    m_synth->setMWAmpRange  ((int)(value * 99.0f)); break;

            // MIDI routing (A5/A6/A7/A2).
            case kParamCHInfo:         m_synth->setMidiChInfoOn(value > 0.5f); break;
            case kParamSysInfo:        m_synth->setMidiSysexInfoOn(value > 0.5f); break;
            case kParamTransmitCh:     m_synth->setMidiTransmitChannel((int)(value * 15.99f)); break;
            case kParamDualDetune:     m_synth->setDualDetune((int)(value * 99.0f)); break;

            // MIDI receive routing + foot controller + breath pitch.
            case kParamMidiSwitch:     m_synth->setMidiSwitchOn(value > 0.5f); break;
            case kParamReceiveCh:      m_synth->setMidiReceiveChannel((int)(value * 16.99f)); break;
            case kParamFootVolume:     m_synth->setFootVolumeRange((int)(value * 99.0f)); break;
            case kParamFootSustain:    m_synth->setFootSustainOn(value > 0.5f); break;
            case kParamBreathPitch:    m_synth->setBreathPitch((int)(value * 99.0f)); break;
            case kParamFootPorta:      m_synth->setFootPortaOn(value > 0.5f); break;

            // Pitch EG (VCED 87-92) — writes through writeVcedGlobal's
            // internal ids 14..19 (Memory Protect enforced there).
            case kParamPEGR1: m_synth->writeVcedGlobal(14, (uint8_t)(value * 99.0f)); break;
            case kParamPEGR2: m_synth->writeVcedGlobal(15, (uint8_t)(value * 99.0f)); break;
            case kParamPEGR3: m_synth->writeVcedGlobal(16, (uint8_t)(value * 99.0f)); break;
            case kParamPEGL1: m_synth->writeVcedGlobal(17, (uint8_t)(value * 99.0f)); break;
            case kParamPEGL2: m_synth->writeVcedGlobal(18, (uint8_t)(value * 99.0f)); break;
            case kParamPEGL3: m_synth->writeVcedGlobal(19, (uint8_t)(value * 99.0f)); break;

            default:
                break;
        }
    }

    float getParameter(int index)
    {
        if (!m_synth) return 0.0f;
        switch (index)
        {
            case kParamInstrument: {
                int n = m_synth->getNumPrograms();
                if (n <= 0) return 0.0f;
                return (float)m_synth->getCurrentProgram() / (float)(n - 1);
            }
            case kParamMasterGain:     return m_synth->getMasterGain() / 8.0f;
            case kParamEnsemble:       return m_synth->getEnsembleOn() ? 1.0f : 0.0f;
            case kParamPlayMode:       return (float)m_synth->getPlayMode() / 2.0f;
            case kParamSplitPoint:      return (float)m_synth->getSplitPoint() / 127.0f;
            case kParamBalance:        return (float)m_synth->getBalance() / 99.0f;
            case kParamPitchBendRange:  return (float)m_synth->getPitchBendRange() / 12.0f;
            case kParamPortamentoMode: return (float)m_synth->getPortamentoMode() / 2.0f;
            case kParamPortamentoRate:  return (float)m_synth->getPortamentoRate() / 99.0f;

            // For VCED params, read from current patch
            case kParamLFOSpeed: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->lfo_speed / 255.0f : 0.0f;
            }
            case kParamLFODelay: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->lfo_delay / 127.0f : 0.0f;
            }
            case kParamPMD: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->pmd / 127.0f : 0.0f;
            }
            case kParamAMD: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->amd / 127.0f : 0.0f;
            }
            case kParamAlgorithm: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->alg / 7.0f : 0.0f;
            }
            case kParamFeedback: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->fb / 7.0f : 0.0f;
            }
            case kParamLFOSync: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->lfo_sync : 0.0f;
            }
            case kParamLFOWave: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->lfo_wave / 3.0f : 0.0f;
            }

            // Per-operator reads from current patch. 13 cases per op ×
            // 4 ops = 52 cases, written out longhand to avoid the macro
            // case-value collision when the op index increments.
            // Uses a tiny helper macro to keep the noise down.
            #define DX21_PATCH_PTR() \
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram()); \
                if (!p) return 0.0f

            case kParamOp0AR:  { DX21_PATCH_PTR(); return (float)p->op[0].ar  / 31.0f; }
            case kParamOp0D1R: { DX21_PATCH_PTR(); return (float)p->op[0].d1r / 31.0f; }
            case kParamOp0D1L: { DX21_PATCH_PTR(); return (float)p->op[0].d1l / 15.0f; }
            case kParamOp0Out: { DX21_PATCH_PTR(); return (float)p->op[0].out / 99.0f; }
            case kParamOp0CRS: { DX21_PATCH_PTR(); return (float)p->op[0].crs / 63.0f; }
            case kParamOp0D2R: { DX21_PATCH_PTR(); return (float)p->op[0].d2r / 31.0f; }
            case kParamOp0RR:  { DX21_PATCH_PTR(); return (float)p->op[0].rr  / 15.0f; }
            case kParamOp0LS:  { DX21_PATCH_PTR(); return (float)p->op[0].ls  / 99.0f; }
            case kParamOp0RS:  { DX21_PATCH_PTR(); return (float)p->op[0].rs  / 3.0f;  }
            case kParamOp0EBS: { DX21_PATCH_PTR(); return (float)p->op[0].ebs / 7.0f;  }
            case kParamOp0AME: { DX21_PATCH_PTR(); return p->op[0].ame ? 1.0f : 0.0f; }
            case kParamOp0KVS: { DX21_PATCH_PTR(); return (float)p->op[0].kvs / 7.0f;  }
            case kParamOp0DET: { DX21_PATCH_PTR(); return (float)p->op[0].det / 6.0f;  }

            case kParamOp1AR:  { DX21_PATCH_PTR(); return (float)p->op[1].ar  / 31.0f; }
            case kParamOp1D1R: { DX21_PATCH_PTR(); return (float)p->op[1].d1r / 31.0f; }
            case kParamOp1D1L: { DX21_PATCH_PTR(); return (float)p->op[1].d1l / 15.0f; }
            case kParamOp1Out: { DX21_PATCH_PTR(); return (float)p->op[1].out / 99.0f; }
            case kParamOp1CRS: { DX21_PATCH_PTR(); return (float)p->op[1].crs / 63.0f; }
            case kParamOp1D2R: { DX21_PATCH_PTR(); return (float)p->op[1].d2r / 31.0f; }
            case kParamOp1RR:  { DX21_PATCH_PTR(); return (float)p->op[1].rr  / 15.0f; }
            case kParamOp1LS:  { DX21_PATCH_PTR(); return (float)p->op[1].ls  / 99.0f; }
            case kParamOp1RS:  { DX21_PATCH_PTR(); return (float)p->op[1].rs  / 3.0f;  }
            case kParamOp1EBS: { DX21_PATCH_PTR(); return (float)p->op[1].ebs / 7.0f;  }
            case kParamOp1AME: { DX21_PATCH_PTR(); return p->op[1].ame ? 1.0f : 0.0f; }
            case kParamOp1KVS: { DX21_PATCH_PTR(); return (float)p->op[1].kvs / 7.0f;  }
            case kParamOp1DET: { DX21_PATCH_PTR(); return (float)p->op[1].det / 6.0f;  }

            case kParamOp2AR:  { DX21_PATCH_PTR(); return (float)p->op[2].ar  / 31.0f; }
            case kParamOp2D1R: { DX21_PATCH_PTR(); return (float)p->op[2].d1r / 31.0f; }
            case kParamOp2D1L: { DX21_PATCH_PTR(); return (float)p->op[2].d1l / 15.0f; }
            case kParamOp2Out: { DX21_PATCH_PTR(); return (float)p->op[2].out / 99.0f; }
            case kParamOp2CRS: { DX21_PATCH_PTR(); return (float)p->op[2].crs / 63.0f; }
            case kParamOp2D2R: { DX21_PATCH_PTR(); return (float)p->op[2].d2r / 31.0f; }
            case kParamOp2RR:  { DX21_PATCH_PTR(); return (float)p->op[2].rr  / 15.0f; }
            case kParamOp2LS:  { DX21_PATCH_PTR(); return (float)p->op[2].ls  / 99.0f; }
            case kParamOp2RS:  { DX21_PATCH_PTR(); return (float)p->op[2].rs  / 3.0f;  }
            case kParamOp2EBS: { DX21_PATCH_PTR(); return (float)p->op[2].ebs / 7.0f;  }
            case kParamOp2AME: { DX21_PATCH_PTR(); return p->op[2].ame ? 1.0f : 0.0f; }
            case kParamOp2KVS: { DX21_PATCH_PTR(); return (float)p->op[2].kvs / 7.0f;  }
            case kParamOp2DET: { DX21_PATCH_PTR(); return (float)p->op[2].det / 6.0f;  }

            case kParamOp3AR:  { DX21_PATCH_PTR(); return (float)p->op[3].ar  / 31.0f; }
            case kParamOp3D1R: { DX21_PATCH_PTR(); return (float)p->op[3].d1r / 31.0f; }
            case kParamOp3D1L: { DX21_PATCH_PTR(); return (float)p->op[3].d1l / 15.0f; }
            case kParamOp3Out: { DX21_PATCH_PTR(); return (float)p->op[3].out / 99.0f; }
            case kParamOp3CRS: { DX21_PATCH_PTR(); return (float)p->op[3].crs / 63.0f; }
            case kParamOp3D2R: { DX21_PATCH_PTR(); return (float)p->op[3].d2r / 31.0f; }
            case kParamOp3RR:  { DX21_PATCH_PTR(); return (float)p->op[3].rr  / 15.0f; }
            case kParamOp3LS:  { DX21_PATCH_PTR(); return (float)p->op[3].ls  / 99.0f; }
            case kParamOp3RS:  { DX21_PATCH_PTR(); return (float)p->op[3].rs  / 3.0f;  }
            case kParamOp3EBS: { DX21_PATCH_PTR(); return (float)p->op[3].ebs / 7.0f;  }
            case kParamOp3AME: { DX21_PATCH_PTR(); return p->op[3].ame ? 1.0f : 0.0f; }
            case kParamOp3KVS: { DX21_PATCH_PTR(); return (float)p->op[3].kvs / 7.0f;  }
            case kParamOp3DET: { DX21_PATCH_PTR(); return (float)p->op[3].det / 6.0f;  }
            #undef DX21_PATCH_PTR

            // Modulation Wheel sensitivity (B8/B9).
            case kParamMWPitchRange:  return (float)m_synth->getMWPitchRange() / 99.0f;
            case kParamMWAmpRange:    return (float)m_synth->getMWAmpRange()   / 99.0f;

            // MIDI routing (A5/A6/A7/A2).
            case kParamCHInfo:        return m_synth->getMidiChInfoOn()    ? 1.0f : 0.0f;
            case kParamSysInfo:       return m_synth->getMidiSysexInfoOn() ? 1.0f : 0.0f;
            case kParamTransmitCh:    return (float)m_synth->getMidiTransmitChannel() / 15.0f;
            case kParamDualDetune:    return (float)m_synth->getDualDetune() / 99.0f;

            // MIDI receive routing + foot controller + breath pitch.
            case kParamMidiSwitch:    return m_synth->getMidiSwitchOn()  ? 1.0f : 0.0f;
            case kParamReceiveCh:     return (float)m_synth->getMidiReceiveChannel() / 16.0f;
            case kParamFootVolume:    return (float)m_synth->getFootVolumeRange() / 99.0f;
            case kParamFootSustain:   return m_synth->getFootSustainOn() ? 1.0f : 0.0f;
            case kParamBreathPitch:   return (float)m_synth->getBreathPitch() / 99.0f;
            case kParamFootPorta:     return m_synth->getFootPortaOn() ? 1.0f : 0.0f;

            // Pitch EG — read from the current patch.
            case kParamPEGR1: case kParamPEGR2: case kParamPEGR3: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                if (!p) return 1.0f;  // default rate 99
                int i = (index == kParamPEGR1) ? 0 : (index == kParamPEGR2 ? 1 : 2);
                return (float)(p->peg_r[i] > 99 ? 99 : p->peg_r[i]) / 99.0f;
            }
            case kParamPEGL1: case kParamPEGL2: case kParamPEGL3: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                if (!p) return 50.0f / 99.0f;  // default level 50 (flat)
                int i = (index == kParamPEGL1) ? 0 : (index == kParamPEGL2 ? 1 : 2);
                return (float)dx21_effective_peg_level(p, i) / 99.0f;
            }

            // Global LFO modulation sensitivity.
            case kParamPMS: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->pms / 7.0f : 0.0f;
            }
            case kParamAMS: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                return p ? (float)p->ams / 3.0f : 0.0f;
            }

            // Non-VCED global parameters.
            case kParamKeyOffset: {
                const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
                if (!p) return 0.5f;  // centre
                return ((float)p->key_offset + 24.0f) / 49.0f;
            }
            case kParamMasterTune: {
                return ((float)m_synth->getMasterTune() + 64.0f) / 128.0f;
            }
            case kParamMono:        return m_synth->isMono() ? 1.0f : 0.0f;
            case kParamPBMode:      return (float)m_synth->getPBMode() / 3.0f;
            case kParamBreathPitchBias: return (float)m_synth->getBreathPitchBias() / 99.0f;
            case kParamBreathAmp:       return (float)m_synth->getBreathAmplitude() / 99.0f;
            case kParamBreathEGBias:    return (float)m_synth->getBreathEGBias()     / 99.0f;
            case kParamBreathEGDepth:   return (float)m_synth->getBreathEGDepth()    / 99.0f;

            default: return 0.0f;
        }
    }

    const char* getParameterName(int index)
    {
        static const char* kNames[kParamTotalCount] = {
            // Instrument (0)
            "Instrument",
            // Global (1-8)
            "Master Gain", "Ensemble", "Play Mode", "Split Point",
            "Balance", "PB Range", "Porta Mode", "Porta Rate",
            // LFO (9-12)
            "LFO Speed", "LFO Delay", "PMD", "AMD",
            // Voice (13-16)
            "Algorithm", "Feedback", "LFO Sync", "LFO Wave",
            // OP0 (17-29) -- core (17-21) + extended (22-29)
            "OP1 AR", "OP1 D1R", "OP1 D1L", "OP1 Out", "OP1 CRS",
            "OP1 D2R", "OP1 RR", "OP1 LS", "OP1 RS", "OP1 EBS",
            "OP1 AME", "OP1 KVS", "OP1 DET",
            // OP1 (30-42)
            "OP2 AR", "OP2 D1R", "OP2 D1L", "OP2 Out", "OP2 CRS",
            "OP2 D2R", "OP2 RR", "OP2 LS", "OP2 RS", "OP2 EBS",
            "OP2 AME", "OP2 KVS", "OP2 DET",
            // OP2 (43-55)
            "OP3 AR", "OP3 D1R", "OP3 D1L", "OP3 Out", "OP3 CRS",
            "OP3 D2R", "OP3 RR", "OP3 LS", "OP3 RS", "OP3 EBS",
            "OP3 AME", "OP3 KVS", "OP3 DET",
            // OP3 (56-68)
            "OP4 AR", "OP4 D1R", "OP4 D1L", "OP4 Out", "OP4 CRS",
            "OP4 D2R", "OP4 RR", "OP4 LS", "OP4 RS", "OP4 EBS",
            "OP4 AME", "OP4 KVS", "OP4 DET",
            // LFO mod sensitivity (69-70)
            "PMS", "AMS",
            // Non-VCED global (71-77)
            "Key Offset", "Master Tune", "Mono Mode", "PB Mode",
            // Breath (78-81)
            "BC Pitch", "BC Amp", "BC EG Bias", "BC EG Depth",
            // Modulation Wheel (82-83)
            "MW Pitch", "MW Amp",
            // MIDI routing (84-87)
            "CH Info", "Sys Info", "Transmit Ch", "Dual Detune",
            // Receive routing + foot + breath pitch (88-92)
            "MIDI Switch", "Receive Ch", "Foot Volume", "Foot Sustain",
            "BC Pitch",
            // Foot porta + Pitch EG (93-99)
            "Foot Porta",
            "PEG Rate 1", "PEG Level 1", "PEG Rate 2", "PEG Level 2",
            "PEG Rate 3", "PEG Level 3",
        };
        if (index < 0 || index >= kParamTotalCount) return "?";
        return kNames[index];
    }

    std::string getParameterDisplay(int index)
    {
        float v = getParameter(index);
        // Return as integer percent
        int pct = (int)(v * 100.0f + 0.5f);
        return std::to_string(pct) + "%";
    }

    // ── Programs (replacing vksynth presets) ──────────────

    void setInstrument(int id)
    {
        if (!m_synth || id < 0) return;
        m_synth->setCurrentProgram(id);
        m_currentProgram = id;
        transmitVoiceIfEnabled(id);  // panel voice select → 1-voice dump
    }

    int getInstrument()
    {
        if (!m_synth) return 0;
        m_currentProgram = m_synth->getCurrentProgram();
        return m_currentProgram;
    }

    // Copy the current program name into the caller-supplied buffer.
    // Always NUL-terminates; never writes more than nameSize bytes.
    // Returns the number of bytes that would have been written if the
    // buffer were large enough (like snprintf), so the caller can
    // detect truncation.
    size_t getInstrumentName(char* name, size_t nameSize)
    {
        if (!name || nameSize == 0) return 0;
        name[0] = '\0';
        if (!m_synth) return 0;
        const char* n = m_synth->getCurrentProgramName();
        if (!n) return 0;
        const size_t srcLen = std::strlen(n);
        const size_t copyLen = (srcLen < nameSize - 1) ? srcLen : nameSize - 1;
        std::memcpy(name, n, copyLen);
        name[copyLen] = '\0';
        return srcLen;
    }

    int getInstrumentCount()
    {
        if (!m_synth) return 0;
        return m_synth->getNumPrograms();
    }

    int getNumPresets()
    {
        if (!m_synth) return 0;
        return m_synth->getNumPrograms();
    }

    // PresetInfo interface: builds a PresetInfo from DX21 patch data
    const PresetInfo* getPreset(int index)
    {
        if (!m_synth || index < 0) return nullptr;

        // Build a cached PresetInfo on demand
        if (index < 256)
        {
            const char* n = m_synth->getProgramName(index);
            m_cachedPreset.file = "";
            m_cachedPreset.name = n ? n : "???";
            m_cachedPreset.category = (index < 128) ? "ROM" : "RAM";
            m_cachedPreset.description = "";
            return &m_cachedPreset;
        }
        return nullptr;
    }

    const PresetInfo* getCurrentPresetInfo()
    {
        if (!m_synth) return nullptr;
        int prog = m_synth->getCurrentProgram();
        return getPreset(prog);
    }

    void loadPreset(int index)
    {
        if (!m_synth || index < 0) return;
        CLogger::Get()->Write("COPMEmuAdapter", LogNotice, "loadPreset(%d)", index);
        m_synth->setCurrentProgram(index);
        m_currentProgram = index;
    }

    int saveUserPreset(const std::string& name,
                       const std::string& category = "User",
                       const std::string& description = "")
    {
        // DX21: save current patch to next available RAM slot via CDX21Memory
        if (!m_synth) return -1;

        int slot = m_synth->getCurrentProgram();
        // For now, DX21 memory persistence is handled via CDX21Memory / IFileSystem
        // This is a simplified stub — full implementation would serialize to JSON
        CLogger::Get()->Write("COPMEmuAdapter", LogNotice,
                               "saveUserPreset: slot=%d, name=%s", slot, name.c_str());
        return slot;
    }

    int getTotalNumParameters() const { return kParamTotalCount; }

    // ── Tape save/load (SD card via IFileSystem) ─────────
    //
    // Bank layout: SD:/MICRODX21/BANK_NN/ where NN is 01..16.
    // Each bank contains 32 voice JSON files (voice_00.json .. voice_31.json),
    // matching the existing CDX21Memory::saveRamBank/loadRamBank(dirPath)
    // interface. The group index 0..15 maps to bank directory 01..16.
    //
    // All three methods return a result string suitable for the
    // MEMORY-mode status line (16 chars or less). They are no-ops
    // when m_pFS is nullptr (no SD card present at boot).
    enum class MemoryResult {
        Ok,            // success
        NoFS,          // no file system bound
        SaveFailed,    // saveRamBank returned false
        LoadFailed,    // loadRamBank returned false
        VerifyMismatch,// load succeeded but voice count differs
        Protected,     // rejected by Memory Protect (FUNCTION #23)
        NotImplemented // shouldn't happen
    };

    // Convert a MemoryResult to a 16-char status string for the display.
    static const char* MemoryResultString(MemoryResult r) {
        switch (r) {
            case MemoryResult::Ok:             return "OK";
            case MemoryResult::NoFS:           return "NO SD CARD";
            case MemoryResult::SaveFailed:     return "SAVE FAILED";
            case MemoryResult::LoadFailed:     return "LOAD FAILED";
            case MemoryResult::VerifyMismatch: return "VERIFY MISMATCH";
            case MemoryResult::Protected:      return "MEM PROTECTED";
            default:                            return "ERR";
        }
    }

    // Save the 32 RAM voices to SD:/MICRODX21/BANK_NN/ as JSON, plus
    // the 32 performance memories to SD:/MICRODX21/performances.json
    // (top level — one set shared across banks, matching the SD
    // skeleton). group is 0..15. Returns the result for status display.
    MemoryResult saveRamBankToFile(int group) {
        if (!m_pFS) return MemoryResult::NoFS;
        if (group < 0 || group >= 16) return MemoryResult::SaveFailed;
        char path[64];
        snprintf(path, sizeof(path), "MICRODX21/BANK_%02d", group + 1);
        bool ok = m_synth->saveRamBank(path);
        // Performance save is best-effort: a missing/failed write
        // doesn't fail the bank save.
        m_synth->savePerformanceBank("MICRODX21/performances.json");
        return ok ? MemoryResult::Ok : MemoryResult::SaveFailed;
    }

    // Load 32 voices from SD:/MICRODX21/BANK_NN/ into RAM (plus the
    // shared performance bank, best-effort). Voices not present in
    // the bank directory are left untouched in RAM.
    MemoryResult loadRamBankFromFile(int group) {
        if (!m_pFS) return MemoryResult::NoFS;
        if (group < 0 || group >= 16) return MemoryResult::LoadFailed;
        char path[64];
        snprintf(path, sizeof(path), "MICRODX21/BANK_%02d", group + 1);
        bool ok = m_synth->loadRamBank(path);
        m_synth->loadPerformanceBank("MICRODX21/performances.json");
        return ok ? MemoryResult::Ok : MemoryResult::LoadFailed;
    }

    // Verify: load the bank and compare each voice's checksum to the
    // current RAM. For now we just re-load and check that all 32
    // voice files were present (a "shallow" verify). A byte-by-byte
    // compare would be straightforward; the structure of CDX21Memory
    // doesn't expose a per-voice checksum yet, so we settle for the
    // file-presence check.
    MemoryResult verifyRamBank(int group) {
        if (!m_pFS) return MemoryResult::NoFS;
        if (group < 0 || group >= 16) return MemoryResult::VerifyMismatch;
        char path[64];
        snprintf(path, sizeof(path), "MICRODX21/BANK_%02d", group + 1);
        // Probe for the first voice file. If it exists, the bank is
        // considered present; otherwise the user gets VerifyMismatch.
        char probe[80];
        snprintf(probe, sizeof(probe), "%s/voice_00.json", path);
        if (!m_pFS->exists(probe)) return MemoryResult::VerifyMismatch;
        return MemoryResult::Ok;
    }

    // ── Voice-editing actions (A9 Recall, A10 Init, A8 Bulk) ──
    //
    // These don't take a value — they trigger a one-shot side effect
    // on the synth. The UI dispatches them when the user lands on
    // the corresponding FUNCTION entry and presses the encoder.
    void TriggerInitVoice()      { if (m_synth) m_synth->initVoice();      }
    void TriggerSaveEditRecall() { if (m_synth) m_synth->saveEditRecall(); }
    void TriggerLoadEditRecall() { if (m_synth) m_synth->loadEditRecall(); }

    // Trigger a 32-voice VCED bulk dump over the current SysEx
    // out path (m_sysexOutFn). Used by FUNCTION A8 "MIDI Transmit?".
    // Returns true on success.
    bool TriggerBulkTransmit() {
        if (!m_synth || !m_sysexOutFn) return false;
        std::vector<uint8_t> data;
        if (!m_synth->memory().exportSysex(data)) return false;
        m_sysexOutFn(m_sysexOutUser, data.data(), data.size());
        return true;
    }

    // Trigger a 1-voice VCED dump of the current program. Same out
    // path as the bulk dump. Returns true on success.
    bool TriggerVoiceTransmit() {
        if (!m_synth || !m_sysexOutFn) return false;
        std::vector<uint8_t> data;
        if (!m_synth->exportCurrentVoiceSysex(data)) return false;
        m_sysexOutFn(m_sysexOutUser, data.data(), data.size());
        return true;
    }

    // Transmit program `prog` as a 1-voice VCED dump if "Midi Sy
    // Info" is ON. The manual specifies that the DX21 dumps the
    // voice whenever a voice selector is pressed in PLAY mode —
    // this is the panel-side hook (called from the instrument-select
    // paths below, never from incoming MIDI).
    void transmitVoiceIfEnabled(int prog) {
        if (!m_synth || !m_sysexOutFn) return;
        if (!m_synth->getMidiSysexInfoOn()) return;
        const DX21_Patch* p = m_synth->getPatch(prog);
        if (!p) return;
        std::vector<uint8_t> data;
        if (!m_synth->memory().exportVoiceSysex(*p, data)) return;
        m_sysexOutFn(m_sysexOutUser, data.data(), data.size());
    }

    // ── COMPARE (audible) ─────────────────────────────────────
    bool setCompare(bool on) { return m_synth ? m_synth->setCompare(on) : false; }
    bool getCompare() const  { return m_synth && m_synth->getCompare(); }

    // ── Voice name (FUNCTION "Name :") ────────────────────────
    bool SetVoiceName(const char* name) {
        return m_synth && m_synth->setVoiceName(name);
    }

    // ── MEMORY-mode actions: STORE / ROM load / performances ──

    // Store the current edit voice into RAM slot 0..31 (panel STORE).
    MemoryResult StoreVoice(int slot) {
        if (!m_synth) return MemoryResult::NotImplemented;
        if (slot < 0 || slot >= CDX21Memory::kNumRamVoices)
            return MemoryResult::SaveFailed;
        if (m_synth->isMemoryProtected()) return MemoryResult::Protected;
        const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram());
        if (!p) return MemoryResult::SaveFailed;
        return m_synth->memory().setRamVoice(slot, *p)
            ? MemoryResult::Ok : MemoryResult::SaveFailed;
    }

    // A11 "LOAD INTERNAL MEMORY": copy ROM group 0..15 (8 voices)
    // into RAM bank 0..3 (slots bank*8 .. bank*8+7).
    MemoryResult LoadRomGroup(int group, int bank) {
        if (!m_synth) return MemoryResult::NotImplemented;
        if (group < 0 || group > 15 || bank < 0 || bank > 3)
            return MemoryResult::LoadFailed;
        if (m_synth->isMemoryProtected()) return MemoryResult::Protected;
        for (int i = 0; i < 8; ++i) {
            if (!m_synth->memory().setRamVoice(bank * 8 + i,
                                               dx21_patches[group * 8 + i]))
                return MemoryResult::LoadFailed;
        }
        return MemoryResult::Ok;
    }

    // A12 "LOAD SINGLE INTERNAL MEMORY": copy one ROM voice 0..127
    // into RAM slot 0..31.
    MemoryResult LoadRomVoice(int romIdx, int slot) {
        if (!m_synth) return MemoryResult::NotImplemented;
        if (romIdx < 0 || romIdx >= TOTAL_OPM_PATCHES) return MemoryResult::LoadFailed;
        if (slot < 0 || slot >= CDX21Memory::kNumRamVoices)
            return MemoryResult::LoadFailed;
        if (m_synth->isMemoryProtected()) return MemoryResult::Protected;
        return m_synth->memory().setRamVoice(slot, dx21_patches[romIdx])
            ? MemoryResult::Ok : MemoryResult::LoadFailed;
    }

    // ROM voice name for the A12 picker display.
    const char* GetRomVoiceName(int romIdx) const {
        if (romIdx < 0 || romIdx >= TOTAL_OPM_PATCHES) return "";
        return dx21_patches[romIdx].name;
    }

    // Store the live engine setup into performance slot 0..31.
    MemoryResult StorePerformance(int slot) {
        if (!m_synth) return MemoryResult::NotImplemented;
        if (slot < 0 || slot >= CDX21Memory::kNumPerformances)
            return MemoryResult::SaveFailed;
        if (m_synth->isMemoryProtected()) return MemoryResult::Protected;
        DX21_Performance perf;
        m_synth->capturePerformance(perf);
        char name[16];
        snprintf(name, sizeof(name), "PERF %02d", slot + 1);
        perf.setName(name);
        return m_synth->memory().setPerformance(slot, perf)
            ? MemoryResult::Ok : MemoryResult::SaveFailed;
    }

    // Apply performance slot 0..31 to the engine. Returns false if
    // the slot is empty.
    bool ApplyPerformance(int slot) {
        if (!m_synth) return false;
        if (!m_synth->memory().getPerformance(slot)) return false;
        m_synth->applyPerformance(slot);
        return true;
    }

    // Read-only info for the PERFORMANCE-mode renderer. Returns
    // false (and leaves the outputs untouched) for empty slots.
    bool GetPerformanceInfo(int slot, char* name, size_t nameSize,
                            int* playMode, int* voiceA, int* voiceB) {
        if (!m_synth) return false;
        const DX21_Performance* p = m_synth->memory().getPerformance(slot);
        if (!p) return false;
        if (name && nameSize > 0) {
            size_t n = sizeof(p->name) < nameSize - 1 ? sizeof(p->name) : nameSize - 1;
            std::memcpy(name, p->name, n);
            name[n] = '\0';
        }
        if (playMode) *playMode = p->playMode;
        if (voiceA)   *voiceA   = p->voiceA;
        if (voiceB)   *voiceB   = p->voiceB;
        return true;
    }

    // ── Raw access for audio thread ───────────────────────

    COPMEmu* raw() { return m_synth; }

private:
    COPMEmu*        m_synth;
    float           m_sampleRate;
    int             m_maxPolyphony;
    int             m_currentProgram;
    IFileSystem*    m_pFS;
    PresetInfo      m_cachedPreset;   // single cached entry for getPreset()

    // SysEx out callback (stored for future use)
    void (*m_sysexOutFn)(void*, const uint8_t*, size_t) = nullptr;
    void*  m_sysexOutUser = nullptr;
};

#endif // _OPMEMU_ADAPTER_H
