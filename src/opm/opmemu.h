#pragma once

#include "opm.h"
#include "patches.h"
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
    COPMEmu();

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
    };

    opm_t    m_chip;
    Voice    m_voices[kNumVoices];
    int      m_currentPatch;
    bool     m_sustainPedal;
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

    void writeReg(uint8_t reg, uint8_t val);
    void writeField(uint8_t addr, unsigned ls_bit, unsigned bits, uint8_t data);
    void clockChip(int cycles);  // clock OPM without collecting audio output

    // VCED → YM2151 register conversion
    void applyPatchToVoice(int voice, const DX21_Patch& patch);  // all static params
    void applyTL(int voice, const DX21_Patch& patch, int midiNote, int velocity);  // per-note TL
    void setFrequency(int voice, bool keyOn);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    int  allocVoice(int note);

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
