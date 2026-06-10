#pragma once

#include "opm.h"
#include "patches.h"
#include "memory/dx21_memory.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <atomic>

// Second-order biquad low-pass filter section (Butterworth)
// Used to reconstruct the DAC output at the target sample rate,
// removing high-frequency aliasing artifacts from the OPM output.
// Supports Butterworth cascading: pass section=0 for Q≈0.5412 (1st section)
// or section=1 for Q≈1.3066 (2nd section) of a 4th-order Butterworth filter.
struct BiquadFilter {
    float b0, b1, b2, a1, a2;   // coefficients
    float x1, x2, y1, y2;       // state (input/output history)

    void init(float sampleRate, float cutoffHz, int section) {
        float omega = 2.0f * M_PI * cutoffHz / sampleRate;
        float sn = sinf(omega);
        float cs = cosf(omega);
        // 4th-order Butterworth: two cascaded biquad sections
        // Section 0: Q = 1/(2*sin(pi/8))  ≈ 0.5412
        // Section 1: Q = 1/(2*sin(3*pi/8)) ≈ 1.3066
        float q = (section == 0) ? 0.5411961001f : 1.3065629649f;
        float alpha = sn / (2.0f * q);
        float a0 = 1.0f + alpha;
        b0 = (1.0f - cs) / (2.0f * a0);
        b1 = (1.0f - cs) / a0;
        b2 = (1.0f - cs) / (2.0f * a0);
        a1 = -2.0f * cs / a0;
        a2 = (1.0f - alpha) / a0;
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

    // COPMEmuAdapter is the application-level wrapper that needs
    // access to noteOn/noteOff, getPatch, writeVcedGlobal/Operator.
    friend class COPMEmuAdapter;

public:
    // Test helpers — public back-door so unit tests can poke
    // writeVcedGlobal/Operator without exposing them on the
    // regular public API. The struct is defined here; access
    // control comes from the friend declaration further down.
    struct TestDoor {
        static void writeVcedGlobal(COPMEmu& e, int p, uint8_t v);
        static void writeVcedOperator(COPMEmu& e, int op, int p, uint8_t v);
    };
    friend struct TestDoor;
    explicit COPMEmu(IFileSystem* fs = nullptr);

    void Initialize();
    void processBlock(float* outputL, float* outputR, int numSamples);
    void processMidi(uint8_t* data, int size);

    // --- Voice-state inspection (read-only, safe to call from any thread) ---
    //
    // Used by the unit test suite to verify voice-allocation and
    // sustain-pedal behavior. Returns:
    //   isVoiceActive(i)     — true if voice i is currently in use
    //   isVoiceSustained(i)  — true if voice i is held by sustain pedal
    //   isVoicePlayingNote(i) — true if voice i is currently playing
    //                           the given MIDI note (MIDI 0..127)
    int  getNumVoices() const { return kNumVoices; }
    bool isVoiceActive(int voice) const {
        if (voice < 0 || voice >= kNumVoices) return false;
        return m_voices[voice].active;
    }
    bool isVoiceSustained(int voice) const {
        if (voice < 0 || voice >= kNumVoices) return false;
        return m_voices[voice].sustained;
    }
    bool isVoicePlayingNote(int note) const {
        for (int i = 0; i < kNumVoices; ++i) {
            if (m_voices[i].active && m_voices[i].origNote == note) {
                return true;
            }
        }
        return false;
    }

    // Get the current pitch of voice i in semitones (or 0.0f if voice
    // is inactive or out-of-range). Useful for unit tests and external
    // visualizers that show the current pitch state.
    float getVoicePitch(int voice) const {
        if (voice < 0 || voice >= kNumVoices) return 0.0f;
        if (!m_voices[voice].active) return 0.0f;
        return m_voices[voice].currentPitch;
    }
    // True if voice i is currently gliding (isPorting state)
    bool isVoicePorting(int voice) const {
        if (voice < 0 || voice >= kNumVoices) return false;
        return m_voices[voice].isPorting;
    }

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

    // --- Master Gain ---
    void setMasterGain(float gain); // 0.0 .. 8.0, default 3.0

    // ───────────────────────────────────────────────
    // Safe-shutdown helpers
    // ───────────────────────────────────────────────
    //
    // allNotesOff() — write KeyOff (reg 0x08) to all 8 OPM channels
    // and mark every voice as inactive. The OPM's release phase will
    // still ring out for a few ms until RR ends naturally, but no
    // new notes can be triggered. The DAC plays silence once the
    // release envelopes finish.
    //
    // resetEngine() — allNotesOff + write 0 to TL on all 4 ops of
    // all 8 channels (reg 0x60). This silences the OPM completely
    // within 1 DMA buffer (~10 ms at 48 kHz / 256 chunk) and is the
    // hook called by the panic path.
    //
    // Both are safe to call from the main thread (not the audio ISR).
    // Calling them from the audio thread is undefined — the audio
    // path touches m_chip directly without locking.
    void allNotesOff();
    void resetEngine();

    // ───────────────────────────────────────────────
    // Voice-Editing helpers (A9, A10, A2, A5, A6, A7, B8, B9)
    // ───────────────────────────────────────────────
    //
    // initVoice() — copy a default VCED (the "Initialized Voice"
    // factory preset, comparable to the DX21's "Init. Voice ?"
    // function) into the currently-edited voice slot, and apply
    // it to the OPM. Caller must call applyPatchToVoice() — wait,
    // we do that internally for the active voice (m_sysexEditVoice).
    void initVoice();

    // saveEditRecall() / loadEditRecall() — implement the DX21's
    // "Edit Recall ?" function. The edit buffer is whatever's
    // currently in m_memory.getRamVoice(m_sysexEditVoice); we
    // snapshot it into m_editRecall on save, and restore from
    // m_editRecall on load. The OPM is re-applied on load.
    void saveEditRecall();
    void loadEditRecall();

    // Mod-Wheel / Breath setter accessors. CC#1 / CC#2 are pushed
    // in via processMidiBuffer(), so these are mostly for testing
    // and for the UI encoder (which can simulate a wheel value).
    void setMWValue(int v) {
        if (v < 0)   v = 0;
        if (v > 127) v = 127;
        m_mwValue = v;
    }
    void setMWPitchRange(int v) {
        if (v < 0) v = 0;
        if (v > 99) v = 99;
        m_mwPitchRange = v;
    }
    void setMWAmpRange(int v) {
        if (v < 0) v = 0;
        if (v > 99) v = 99;
        m_mwAmpRange = v;
    }
    int  getMWValue() const       { return m_mwValue; }
    int  getMWPitchRange() const  { return m_mwPitchRange; }
    int  getMWAmpRange() const    { return m_mwAmpRange; }

    // MIDI settings (A5/A6/A7/A2)
    void setMidiChInfoOn(bool on)  { m_chInfoOn = on; }
    void setMidiSysexInfoOn(bool on){ m_sysexInfoOn = on; }
    void setMidiTransmitChannel(int ch) {
        if (ch < 0)  ch = 0;
        if (ch > 15) ch = 15;
        m_midiTransmitCh = ch;
    }
    void setDualDetune(int v) {
        if (v < 0)  v = 0;
        if (v > 99) v = 99;
        m_dualDetune = v;
    }
    bool getMidiChInfoOn() const     { return m_chInfoOn; }
    bool getMidiSysexInfoOn() const  { return m_sysexInfoOn; }
    int  getMidiTransmitChannel() const { return m_midiTransmitCh; }
    int  getDualDetune() const       { return m_dualDetune; }

    // MIDI Switch (FUNCTION #3) and Receive Channel / Omni
    // (FUNCTION #6/#7). The channel filter itself is enforced by
    // CMicroDX21::ShouldAcceptChannel(), which consults these
    // getters — the engine only holds the state so that FUNCTION-
    // mode edits and config land in one place. 0 = Omni, 1..16 =
    // fixed receive channel.
    void setMidiSwitchOn(bool on)      { m_midiSwitchOn = on; }
    bool getMidiSwitchOn() const       { return m_midiSwitchOn; }
    void setMidiReceiveChannel(int ch) {
        if (ch < 0)  ch = 0;
        if (ch > 16) ch = 16;
        m_midiRecvCh = ch;
    }
    int  getMidiReceiveChannel() const { return m_midiRecvCh; }

    // Foot controller routing (FUNCTION #30/#31). When Foot Volume
    // is ON, MIDI CC#4 scales the stereo output (0..127 → 0..1).
    // Disabling it snaps the level back to full so a half-pressed
    // pedal can't mute the synth permanently. When Foot Sustain is
    // OFF, CC#64 is ignored.
    void setFootVolumeOn(bool on) {
        m_footVolumeOn = on;
        if (!on) m_footVolume = 127;
    }
    bool getFootVolumeOn() const  { return m_footVolumeOn; }
    void setFootSustainOn(bool on){ m_footSustainOn = on; }
    bool getFootSustainOn() const { return m_footSustainOn; }

    // ───────────────────────────────────────────────
    // SysEx out (dump transmit)
    // ───────────────────────────────────────────────
    //
    // The callback is invoked with complete F0..F7 frames from the
    // MIDI/main thread (never from the audio callback). It is used
    // for: (a) answering Yamaha dump requests (F0 43 2n ff F7),
    // (b) the FUNCTION "Midi Transmit ?" bulk dump. Transmission is
    // gated on FUNCTION #5 "Midi Sy Info" (m_sysexInfoOn), matching
    // the original DX21 firmware.
    typedef void (*SysexOutFn)(void* user, const uint8_t* data, size_t len);
    void setSysexOutCallback(SysexOutFn fn, void* user) {
        m_sysexOutFn   = fn;
        m_sysexOutUser = user;
    }

    // Build a 1-voice VCED dump (F0 43 0n 03 ...) of the currently
    // selected program. Returns false if the patch is unavailable.
    bool exportCurrentVoiceSysex(std::vector<uint8_t>& out);

    // Constants for the A10 Init Voice (factory defaults).
    // Defined in opmemu.cpp as a static const DX21_Patch.
    static const DX21_Patch* GetInitVoicePatch();
    float getMasterGain() const { return m_masterGain; }

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
    void setBreathPitch(int value);      // 0..99 (FUNCTION #35: BC→LFO PMD range)
    void setBreathPitchBias(int value);  // 0..99
    void setBreathAmplitude(int value);  // 0..99
    void setBreathEGBias(int value);     // 0..99
    void setBreathEGDepth(int value);    // 0..99
    int  getBreathPitch() const { return m_breathPitch; }
    int  getBreathPitchBias() const { return m_breathPitchBias; }
    int  getBreathAmplitude() const { return m_breathAmplitude; }
    int  getBreathEGBias() const { return m_breathEGBias; }
    int  getBreathEGDepth() const { return m_breathEGDepth; }

    // --- SysEx Edit Voice ---
    void setSysexEditVoice(int voice);   // 0..7, which voice slot receives parameter changes
    int  getSysexEditVoice() const { return m_sysexEditVoice; }

    // --- Memory Protect (Function #23 / dat_F610:35) ---
    // When ON, all writes to RAM voices and performance memories are
    // rejected: SysEx bulk dumps, real-time parameter changes, slider
    // edits, init-voice, recall-edit. Reads (getRamVoice, getPatch) and
    // OPM register reads are unaffected. The real DX21 hardware also
    // gates these paths behind this bit.
    void setMemoryProtect(bool on);
    bool isMemoryProtected() const         { return m_memoryProt; }

private:
    static const int      kNumVoices  = 8;
    static const uint32_t kSampleRate = 48000;
    static const int kKeyOnDelay = 256;  // ~5.3ms: TL ramp-up time before KeyOn
    // Nuked OPM: 1 OPM_Clock = 1 internal clock = 1/(3579545/2) seconds
    // Use external clock for fractional accumulator to handle the .5 Hz:
    //   internal_clocks_per_sample = 3579545 / (2 * 48000) = 37 + 27545/96000
    static const uint32_t kFmClockFull     = 3579545;       // YM2151 external clock (Hz)
    static const int      kAttackSamples   = 240;           // ~5 ms fade-in @ 48kHz to mask phase-reset click
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
        int      attackSamples; // remaining attack fade-in samples (0 = fully open)
        int      keyOnDelay;   // >0: KeyOn pending, countdown in audio samples
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
    float    m_masterGain;  // global output gain (headroom compensation)

    // --- Pitch Bend ---
    int      m_pbValue;     // -8192..+8191 (MIDI 14-bit centered)
    int      m_pbRange;     // 0..12 semitones

    // --- Portamento ---
    enum PortaMode { PortaOff, PortaFullTime, PortaFingered };
    PortaMode m_portaMode;
    int       m_portaRate;  // 0..99
    float     m_portaDecayFactor; // per-sample decay multiplier (0.0..1.0)

    // --- PB Mode (Low / High / K-on) ---
    enum PBMode { PBAll, PBLow, PBHigh, PBKOn };
    PBMode m_pbMode;

    // --- Memory Protect (Function #23) ---
    bool m_memoryProt;

    // --- Modulation Wheel (CC#1) + Breath (CC#2) ---
    // Inline initializers: these were previously left uninitialized
    // (no constructor-init-list entry), which made the MW/Breath
    // response and the MIDI-settings defaults depend on stack/heap
    // garbage. Defaults follow the DX21 factory settings.
    int m_mwValue = 0;           // 0..127 (MIDI CC#1)
    int m_mwPitchRange = 50;     // 0..99 (B8: MW→LFO PMD scale)
    int m_mwAmpRange = 0;        // 0..99 (B9: MW→LFO AMD scale)
    int m_breathValue = 0;       // 0..127 (MIDI CC#2)
    int m_breathPitch = 0;       // 0..99  (B10: BC→LFO PMD scale)
    int m_breathPitchBias = 0;   // 0..99  (B12)
    int m_breathAmplitude = 0;   // 0..99  (B11)
    int m_breathEGBias = 0;      // 0..99  (B13)
    int m_breathEGDepth = 0;     // 0..99

    // --- Function-mode MIDI settings ---
    bool m_chInfoOn = true;      // A6: Channel Information ON/OFF
    bool m_sysexInfoOn = true;   // A7: System (SysEx) Information ON/OFF
    int  m_midiTransmitCh = 0;   // A5: 0..15 (0 = off, 1..15 = ch)
    int  m_dualDetune = 0;       // A2: 0..99 (DUAL-mode side-B detune)

    // --- MIDI receive routing (FUNCTION #3/#6/#7) ---
    bool m_midiSwitchOn = true;  // FUNCTION #3: master MIDI receive switch
    int  m_midiRecvCh = 0;       // FUNCTION #6/#7: 0 = Omni, 1..16

    // --- Foot controller (FUNCTION #30/#31) ---
    bool m_footVolumeOn = true;  // CC#4 → output level enable
    bool m_footSustainOn = true; // CC#64 → sustain enable
    int  m_footVolume = 127;     // last CC#4 value (127 = full)

    // --- SysEx out callback (dump transmit) ---
    SysexOutFn m_sysexOutFn = nullptr;
    void*      m_sysexOutUser = nullptr;

    // --- Edit-Recall buffer (A9) ---
    // A 2nd copy of the current edit buffer that A9 "Recall Edit ?"
    // swaps in. The synth keeps this in RAM only — never persisted.
    DX21_Patch m_editRecall;
    bool       m_bEditRecallValid = false;
    uint32_t m_voiceAge;
    uint32_t m_cycleAccum;  // fractional cycle accumulator for pitch accuracy
    int      m_staggerCount;  // remaining staggered noteOns to process

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
    void completeKeyOn(int voice);             // delayed KeyOn: KC/KF + TL + KeyOn
    void updatePortamento(int numSamples);                    // called from processBlock
    float computePortaDecayFactor(int rate) const;
    void writeFrequency(int voice, float pitchSemitones, bool keyOn = false);
    int  findVoiceForPB() const;               // find target voice per PBMode
    void handleBreath(int value);              // MIDI CC#2 handler
    void updateLFOModDepth();                  // MW+BC → reg 0x19 PMD/AMD

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

    // --- 4th-order Butterworth reconstruction filter ---
    // The real DX21/YM2164 has an analog reconstruction filter at the DAC
    // output that rolls off more aggressively than a simple 2nd-order filter.
    // We emulate this with a 4th-order Butterworth LPF at 7.5 kHz (two
    // cascaded biquad sections per channel) for a warmer, softer tone
    // that more closely matches the original hardware.
    // Response: -3 dB @ 7.5 kHz, -13 dB @ 10 kHz, -36 dB @ 15 kHz
    static constexpr float kFilterCutoff = 7500.0f;  // Hz
    BiquadFilter m_filterL1;  // section 0 (Q≈0.54)
    BiquadFilter m_filterL2;  // section 1 (Q≈1.31)
    BiquadFilter m_filterR1;  // section 0 (Q≈0.54)
    BiquadFilter m_filterR2;  // section 1 (Q≈1.31)

    // DC blocker state (per-channel IIR high-pass ~10 Hz cutoff at 48kHz)
    float m_dcLastInL = 0.0f, m_dcLastOutL = 0.0f;
    float m_dcLastInR = 0.0f, m_dcLastOutR = 0.0f;

    // Global soft-attack counter: brief fade-in after every note-on to
    // mask the remaining phase-reset transient that the OPP TL-ramp cannot
    // catch (the very first sample after KeyOn is still non-zero).
    int      m_globalAttackRemaining = 0;

    // --- Pending-Audio Ringpuffer für writeReg/clockChip-Cycles ---
    // Jeder OPM_Clock, der außerhalb des regulären Block-Renderings läuft
    // (Register-Schreibvorgänge, MIDI-Verarbeitung), liefert einen DAC-Sample.
    // Statt diese Samples zu mitteln (DC-Plateau → Klick) puffern wir sie
    // hier roh, damit processBlock sie mit korrekter Decimation in das
    // Output-Buffer einmischen kann. Größe deckt den maximalen Burst eines
    // noteOn ab (~28 writeReg × ~50 Cycles + Reserve).
    static const int kPendingMax = 16384;
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
