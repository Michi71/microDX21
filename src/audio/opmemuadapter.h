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
    // Global (0-7)
    kParamMasterGain = 0,    // 0..1 → master gain 0..8
    kParamEnsemble,          // 0/1 → chorus on/off
    kParamPlayMode,          // 0..1 → Single/Dual, 0..1 → Single/Dual/Split (3 steps)
    kParamSplitPoint,         // 0..1 → MIDI note 0..127
    kParamBalance,            // 0..1 → balance 0..99
    kParamPitchBendRange,     // 0..1 → 0..12 semitones
    kParamPortamentoMode,    // 0..1 → Off/FullTime/Fingered (3 steps)
    kParamPortamentoRate,    // 0..1 → 0..99

    // LFO (8-11)
    kParamLFOSpeed,          // 0..1 → LFO speed 0..255
    kParamLFODelay,           // 0..1 → LFO delay 0..127
    kParamPMD,                // 0..1 → Pitch Mod Depth 0..127
    kParamAMD,                // 0..1 → Amp Mod Depth 0..127

    // Voice common (12-15)
    kParamAlgorithm,          // 0..1 → algorithm 0..7 (8 steps)
    kParamFeedback,            // 0..1 → feedback 0..7
    kParamLFOSync,            // 0/1 → LFO key sync
    kParamLFOWave,            // 0..1 → LFO waveform 0..3 (4 steps)

    // Per-operator parameters (4 ops × 5 core params = 20)
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

    kParamTotalCount
};

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
        // TODO: COPMEmu doesn't have SysEx out callback yet; store for future use
        m_sysexOutFn = fn;
        m_sysexOutUser = user;
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

            // Per-operator parameters (AR, D1R, D1L, OUT, CRS)
            // Each op block is 5 consecutive indices
            #define DX21_OP_PARAMS(op_idx, base) \
                case base:   m_synth->writeVcedOperator(op_idx, 0, (uint8_t)(value * 31.0f)); break; /* AR */  \
                case base+1: m_synth->writeVcedOperator(op_idx, 2, (uint8_t)(value * 31.0f)); break; /* D1R */ \
                case base+2: m_synth->writeVcedOperator(op_idx, 8, (uint8_t)(value * 15.0f)); break; /* D1L */ \
                case base+3: m_synth->writeVcedOperator(op_idx, 10, (uint8_t)(value * 99.0f)); break;/* OUT */ \
                case base+4: m_synth->writeVcedOperator(op_idx, 11, (uint8_t)(value * 63.0f)); break;/* CRS */

            DX21_OP_PARAMS(0, kParamOp0AR)
            DX21_OP_PARAMS(1, kParamOp1AR)
            DX21_OP_PARAMS(2, kParamOp2AR)
            DX21_OP_PARAMS(3, kParamOp3AR)
            #undef DX21_OP_PARAMS

            default:
                break;
        }
    }

    float getParameter(int index)
    {
        if (!m_synth) return 0.0f;
        switch (index)
        {
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

            // Per-operator reads from current patch
            #define DX21_OP_GET(op_idx, base) \
                case base:   { const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram()); return p ? (float)p->op[op_idx].ar / 31.0f : 0.0f; }  \
                case base+1: { const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram()); return p ? (float)p->op[op_idx].d1r / 31.0f : 0.0f; }  \
                case base+2: { const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram()); return p ? (float)p->op[op_idx].d1l / 15.0f : 0.0f; }  \
                case base+3: { const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram()); return p ? (float)p->op[op_idx].out / 99.0f : 0.0f; }  \
                case base+4: { const DX21_Patch* p = m_synth->getPatch(m_synth->getCurrentProgram()); return p ? (float)p->op[op_idx].crs / 63.0f : 0.0f; }

            DX21_OP_GET(0, kParamOp0AR)
            DX21_OP_GET(1, kParamOp1AR)
            DX21_OP_GET(2, kParamOp2AR)
            DX21_OP_GET(3, kParamOp3AR)
            #undef DX21_OP_GET

            default: return 0.0f;
        }
    }

    const char* getParameterName(int index)
    {
        static const char* kNames[kParamTotalCount] = {
            // Global (0-7)
            "Master Gain", "Ensemble", "Play Mode", "Split Point",
            "Balance", "PB Range", "Porta Mode", "Porta Rate",
            // LFO (8-11)
            "LFO Speed", "LFO Delay", "PMD", "AMD",
            // Voice (12-15)
            "Algorithm", "Feedback", "LFO Sync", "LFO Wave",
            // OP0 (16-20)
            "OP1 AR", "OP1 D1R", "OP1 D1L", "OP1 Out", "OP1 CRS",
            // OP1 (21-25)
            "OP2 AR", "OP2 D1R", "OP2 D1L", "OP2 Out", "OP2 CRS",
            // OP2 (26-30)
            "OP3 AR", "OP3 D1R", "OP3 D1L", "OP3 Out", "OP3 CRS",
            // OP3 (31-35)
            "OP4 AR", "OP4 D1R", "OP4 D1L", "OP4 Out", "OP4 CRS",
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
