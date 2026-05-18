#pragma once

#include "opm.h"
#include "patches.h"
#include "memory/dx21_memory.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>

// Second-order biquad low-pass filter (Butterworth)
// Used to reconstruct the DAC output at the target sample rate,
// removing high-frequency aliasing artifacts from the OPM output.
struct BiquadFilter {
    float b0, b1, b2, a1, a2;   // coefficients
    float x1, x2, y1, y2;       // state (input/output history)

    void init(float sampleRate, float cutoffHz) {
        float omega = 2.0f * M_PI * cutoffHz / sampleRate;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float alpha = sn / (2.0f * 0.7071f);  // Butterworth Q = 0.7071
        float a0 = 1.0f + alpha;
        b0 = (1.0f - cs) / (2.0f * a0);
        b1 = (1.0f - cs) / a0;
        b2 = (1.0f - cs) / (2.0f * a0);
        a1 = -2.0f * cs / a0;
        a2 = (1.0f - alpha) / a0;
        // Normalize so b0+b1+b2 = 1 for unity gain at DC
        // (already normalized by a0 division)
        x1 = x2 = y1 = y2 = 0.0f;
    }

    float process(float in) {
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }
};

// ===========================================================================
// Ensemble/Chorus effect — DX21-style stereo chorus
// Based on the GS1 ensemble implementation: 3 modulated delay lines with
// 2 LFOs at ~0.5 Hz and ~3 Hz, 120° phase offsets between lines.
// ===========================================================================
struct EnsembleDelayLine {
    static constexpr int kBufSize = 1024;  // must be power of 2 — fits base+mod at 48kHz
    static constexpr int kBufMask = kBufSize - 1;
    float buffer[kBufSize] = {};
    int writePos = 0;
    float delay = 0.0f;
    int delayInt = 0;
    float delayFrac = 0.0f;

    void setDelay(float delayInSamples) {
        delay = delayInSamples;
        if (delay < 0.0f) delay = 0.0f;
        if (delay > kBufSize - 2) delay = static_cast<float>(kBufSize - 2);
        delayInt = static_cast<int>(delay);
        delayFrac = delay - static_cast<float>(delayInt);
    }

    void pushSample(float sample) {
        buffer[writePos] = sample;
        writePos = (writePos - 1) & kBufMask;
    }

    float popSample() {
        int idx1 = (writePos + delayInt) & kBufMask;
        int idx2 = (idx1 + 1) & kBufMask;
        float s1 = buffer[idx1];
        float s2 = buffer[idx2];
        float result = s1 + (s2 - s1) * delayFrac;
        return result;
    }

    void reset() {
        for (int i = 0; i < kBufSize; i++) buffer[i] = 0.0f;
        writePos = 0;
    }
};

class COPMEmu {

public:
    explicit COPMEmu(IFileSystem* fs = nullptr);

    void Initialize();
    void processBlock(float* outputL, float* outputR, int numSamples);
    void processMidi(uint8_t* data, int size);

    int  getNumPrograms();
    int  getCurrentProgram();
    const char* getCurrentProgramName();
    const char* getProgramName(int index);
    void setCurrentProgram(int index);
    void setEnsembleOn(bool on);
    bool getEnsembleOn();
    // --- Play Modes (DX21 SINGLE / DUAL / SPLIT) ---
    enum PlayMode { Single, Dual, Split };

    void setPlayMode(PlayMode mode);
    PlayMode getPlayMode() const { return m_playMode; }

    void setSplitPoint(int note);   // MIDI note number (default 60 = C3)
    int  getSplitPoint() const { return m_splitPoint; }

    void setBalance(int balance);   // 0..99, 50 = center
    int  getBalance() const { return m_balance; }

    void setMono(bool mono);        // true = MONO, false = POLY
    bool isMono() const { return m_mono; }

    void setPatchA(int index);
    void setPatchB(int index);
    int  getPatchA() const { return m_patchA; }
    int  getPatchB() const { return m_patchB; }

    void setMasterTune(int cents);  // -64..+63
    int  getMasterTune() const { return m_masterTune; }

    // --- Pitch Bend ---
    void setPitchBend(int value);        // 0..16383 (MIDI 14-bit)
    void setPitchBendRange(int range);   // 0..12 semitones
    int  getPitchBendRange() const { return m_pbRange; }

    // --- Portamento ---
    void setPortamentoMode(int mode);    // 0=Off, 1=FullTime, 2=Fingered
    void setPortamentoRate(int rate);    // 0..99
    int  getPortamentoMode() const { return (int)m_portaMode; }
    int  getPortamentoRate() const { return m_portaRate; }

    // --- DX21 Memory (RAM + Performance) ---
    CDX21Memory& memory() { return m_memory; }
    bool loadRamBank(const std::string& dirPath) { return m_memory.loadRamBank(dirPath); }
    bool saveRamBank(const std::string& dirPath) { return m_memory.saveRamBank(dirPath); }
    bool loadPerformanceBank(const std::string& filePath) { return m_memory.loadPerformanceBank(filePath); }
    bool savePerformanceBank(const std::string& filePath) { return m_memory.savePerformanceBank(filePath); }
    void initRamFromRom();

    // Apply a performance to the engine state
    void applyPerformance(int perfSlot);

    // --- PB Mode ---
    void setPBMode(int mode);            // 0=All, 1=Low, 2=High, 3=K-on
    int  getPBMode() const { return (int)m_pbMode; }

    // --- Breath Controller ---
    void setBreathPitchBias(int value);  // 0..99
    void setBreathAmplitude(int value);  // 0..99
    void setBreathEGBias(int value);     // 0..99
    void setBreathEGDepth(int value);    // 0..99
    int  getBreathPitchBias() const { return m_breathPitchBias; }
    int  getBreathAmplitude() const { return m_breathAmplitude; }
    int  getBreathEGBias() const { return m_breathEGBias; }
    int  getBreathEGDepth() const { return m_breathEGDepth; }

    // --- SysEx Edit Voice ---
    void setSysexEditVoice(int voice);   // 0..7, which voice slot receives parameter changes
    int  getSysexEditVoice() const { return m_sysexEditVoice; }

private:
    static const int      kNumVoices  = 8;
    static const uint32_t kSampleRate = 48000;
    // Nuked OPM: 1 OPM_Clock = 1 internal clock = 1/(3579545/2) seconds
    // Use external clock for fractional accumulator to handle the .5 Hz:
    //   internal_clocks_per_sample = 3579545 / (2 * 48000) = 37 + 27545/96000
    static const uint32_t kFmClockFull     = 3579545;       // YM2151 external clock (Hz)
    static const int      kCyclesPerSample = kFmClockFull / (2 * kSampleRate);  // = 37
    static const uint32_t kCycleFracStep   = kFmClockFull % (2 * kSampleRate); // = 27545

    struct Voice {
        bool     active;
        bool     sustained;
        int      origNote;   // original MIDI note (for voice lookup)
        int      note;       // transposed note (for KC calculation)
        int      velocity;
        uint32_t age;
        float    currentPitch; // current pitch in semitones (portamento/pitch bend)
        float    targetPitch;  // target pitch
        bool     isPorting;    // portamento in progress
    };

    IFileSystem* m_fs;
    CDX21Memory  m_memory;

    opm_t    m_chip;
    Voice    m_voices[kNumVoices];
    int      m_currentPatch;   // kept for backward compat; maps to m_patchA
    bool     m_sustainPedal;

    // --- DX21 Performance State ---
    PlayMode m_playMode;
    int      m_splitPoint;    // MIDI note (default 60 = C3)
    int      m_balance;     // 0..99, 50 = center
    bool     m_mono;        // MONO mode
    int      m_patchA;      // patch index for side A
    int      m_patchB;      // patch index for side B
    int      m_masterTune;  // -64..+63

    // --- Pitch Bend ---
    int      m_pbValue;     // -8192..+8191 (MIDI 14-bit centered)
    int      m_pbRange;     // 0..12 semitones

    // --- Portamento ---
    enum PortaMode { PortaOff, PortaFullTime, PortaFingered };
    PortaMode m_portaMode;
    int       m_portaRate;  // 0..99
    float     m_portaRateFactor; // derived from rate (0.0 .. 1.0)

    // --- PB Mode (Low / High / K-on) ---
    enum PBMode { PBAll, PBLow, PBHigh, PBKOn };
    PBMode m_pbMode;

    // --- Breath Controller ---
    int m_breathValue;       // 0..127 (MIDI CC#2)
    int m_breathPitchBias;   // 0..99
    int m_breathAmplitude;   // 0..99
    int m_breathEGBias;      // 0..99
    int m_breathEGDepth;     // 0..99
    uint32_t m_voiceAge;
    uint32_t m_cycleAccum;  // fractional cycle accumulator for pitch accuracy

    // --- Lock-free SPSC MIDI event queue ---
    // processMidi() is the single producer (MIDI/main thread).
    // processMidiBuffer() is the single consumer (audio thread).
    // Power-of-2 ring buffer size enables fast modulo via bitmask.
    static const int kMidiRingSize  = 512; //256;  // must be power of 2
    static const int kMidiRingMask  = kMidiRingSize - 1;
    struct MidiEvent {
        uint8_t status;
        uint8_t data1;
        uint8_t data2;
    };
    MidiEvent     m_midiRing[kMidiRingSize];
    std::atomic<int> m_midiWritePos{0};  // producer increments after write
    std::atomic<int> m_midiReadPos{0};   // consumer increments after read

    // --- SysEx Real-Time Parameter Change ---
    static constexpr int kSysexNumParams = 76;  // DX21 VCED size
    uint8_t  m_sysexParam[kSysexNumParams];
    bool     m_sysexDirty[kSysexNumParams];
    int      m_sysexEditVoice;  // 0..7, which voice slot is being edited

    void handleSysex(const uint8_t* data, int size);
    void applySysexChanges();
    void applySysexParam(int paramIndex, uint8_t value);

    // VCED byte index → OPM register mapping helpers
    void writeVcedGlobal(int param, uint8_t value);
    void writeVcedOperator(int op, int param, uint8_t value);

    // --- Thread-safe program change ---
    // setCurrentProgram() stores the new program number here (main thread).
    // processMidiBuffer() picks it up and applies it on the audio thread,
    // avoiding race conditions on the OPM chip state.
    std::atomic<int> m_pendingProgram{-1};  // -1 = no pending change

    // --- Ensemble/Chorus effect ---
    // DX21-style stereo chorus: 3 modulated delay lines with 120° phase offsets.
    // LFO1 ~0.5 Hz (slow), LFO2 ~3 Hz (fast, lower depth).
    // Mix: 85% slow + 15% fast modulation, just like the GS1.
    bool     m_ensembleOn = false;
    float    m_lfo1Phase = 0.0f;
    float    m_lfo2Phase = 0.0f;
    EnsembleDelayLine m_delayA;
    EnsembleDelayLine m_delayB;
    EnsembleDelayLine m_delayC;

    // Shadow-Register für writeField
    uint8_t  m_shadow[256];

    // --- Pitch / Portamento ---
    void applyPitchToVoice(int voice);         // write KC/KF with PB+porta+tune+breath
    void updatePortamento();                    // called from processBlock
    float computePortaRateFactor(int rate) const;
    void writeFrequency(int voice, float pitchSemitones, bool keyOn = false);
    int  findVoiceForPB() const;               // find target voice per PBMode
    void handleBreath(int value);              // MIDI CC#2 handler

    void writeReg(uint8_t reg, uint8_t val);
    void writeField(uint8_t addr, unsigned ls_bit, unsigned bits, uint8_t data);
    void clockChip(int cycles);  // clock OPM without collecting audio output

    // VCED → YM2151 register conversion
    void applyPatchToVoice(int voice, const DX21_Patch& patch);  // all static params
    void applyTL(int voice, const DX21_Patch& patch, int midiNote, int velocity, int tlOffset = 0);  // per-note TL
    void noteOn(int note, int velocity);
    void noteOff(int note);

    // --- Side-based voice management (DX21 play modes) ---
    enum Side { SideA, SideB };
    int  allocVoice(Side side, int note);
    void freeVoice(int voice);
    Side routeNoteToSide(int note) const;
    int  voiceStart(Side side) const;
    int  voiceCount(Side side) const;
    void applyPatchToSide(Side side, int patchIndex);
    const DX21_Patch* getPatch(int index) const;  // RAM priority, ROM fallback
    void setupVoice(int voice, int note, int velocity, const DX21_Patch& patch);

    // DX21 Level Scaling: computes TL adjustment based on LS and note
    uint8_t computeLSOffset(uint8_t ls, int note) const;
    // DX21 Key Velocity Sensitivity: computes TL adjustment
    uint8_t computeKVSOffset(uint8_t kvs, int velocity) const;

    // --- Biquad low-pass reconstruction filter ---
    // The real YM2151/YM2164 has an analog reconstruction filter at the DAC
    // output. We emulate this with a 2nd-order Butterworth LPF at ~10 kHz
    // to remove high-frequency aliasing and smooth transient clicks.
    static constexpr float kFilterCutoff = 10000.0f;  // Hz
    BiquadFilter m_filterL;
    BiquadFilter m_filterR;

    // --- Pending-Audio Ringpuffer für writeReg/clockChip-Cycles ---
    // Jeder OPM_Clock, der außerhalb des regulären Block-Renderings läuft
    // (Register-Schreibvorgänge, MIDI-Verarbeitung), liefert einen DAC-Sample.
    // Statt diese Samples zu mitteln (DC-Plateau → Klick) puffern wir sie
    // hier roh, damit processBlock sie mit korrekter Decimation in das
    // Output-Buffer einmischen kann. Größe deckt den maximalen Burst eines
    // noteOn ab (~28 writeReg × ~50 Cycles + Reserve).
    static const int kPendingMax = 32768; //= 16384;
    int32_t  m_pendingL[kPendingMax];
    int32_t  m_pendingR[kPendingMax];
    int      m_pendingCount;

    // Apply a pending program change (called from audio thread only)
    void applyProgramChange(int index);

    // Process buffered MIDI events (called from processBlock)
    void processMidiBuffer();

    static float noteToFreq(int note) {
        return 440.0f * powf(2.0f, (note - 69) / 12.0f);
    }
};
