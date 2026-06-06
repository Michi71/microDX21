//
// microdx21.h
//
// Single-core audio: Core 0 runs DMA interrupt (GenerateAudio) +
// main loop (MIDI, deferred preset load).
// Audio-race protection: EnterCritical() around short synth writes
// prevents DMA IRQ from calling processBlock() mid-modification.
// Preset loading uses m_audioPaused (can't disable IRQs during SD I/O).

#ifndef _MICRODX21_H
#define _MICRODX21_H

#include <stdint.h>
#include <string>
#include <cassert>

#include <circle/types.h>
#include <circle/interrupt.h>
#include <circle/synchronize.h>
#include <circle/gpiomanager.h>
#include <circle/i2cmaster.h>
#include <circle/spimaster.h>
#include <circle/sound/soundbasedevice.h>
#include <circle/sched/scheduler.h>
#include <atomic>

#include "config.h"
#include "opmemuadapter.h"
#include "microdx21_usbmididevice.h"
#include "microdx21_serialmididevice.h"
#include "microdx21_pckeyboard.h"
#include "util/ringbuffer.h"

class CMicroDX21MIDIDevice;
class COPMEmuAdapter;


enum MidiSettingID {
    kMidiChannel,
    kMidiVelocityCurve,
    kMidiTranspose,
    kMidiLocalControl,
    kMidiThru,
    kMidiProgramChange,
    kMidiPitchBendRange
};

class CMicroDX21 {
public:
    CMicroDX21(CConfig*          pConfig,
                CInterruptSystem* pInterrupt,
                CGPIOManager*     pGPIOManager,
                CI2CMaster*       pI2CMaster,
                CSPIMaster*       pSPIMaster,
                FATFS*            pFileSystem);
    virtual ~CMicroDX21();

    virtual bool Initialize();
    void Process(bool plugAndPlayUpdated);

    // ───────────────────────────────────────────────
    // MIDI / ENGINE
    // ───────────────────────────────────────────────
    void NoteOn(int32_t note, int32_t velocity);
    void NoteOff(int32_t note);
    void SendMidiCmd(uint8_t cmd, uint8_t data1, uint8_t data2);
    void ForwardMIDI(const u8* msg, size_t len, CMicroDX21MIDIDevice* source);
    void HandleSysExFromDevice(const u8* msg, size_t len, CMicroDX21MIDIDevice* source);

    // Channel filter consulted by CMicroDX21MIDIDevice::HandleMIDI for
    // channel-voice messages (status 0x80-0xEF).  channel0Based is the
    // 4-bit channel field from the MIDI status byte (0..15).
    //
    // m_MidiChannel:
    //   0      = Omni (accept any)
    //   1..16  = accept only when channel0Based + 1 matches.
    //
    // SysEx and System Common/Realtime are NOT routed through this —
    // they always pass so the Web Configurator and clock messages keep
    // working regardless of the channel setting.
    bool ShouldAcceptChannel(u8 channel0Based) const;

    // ───────────────────────────────────────────────
    // PARAMETERS
    // ───────────────────────────────────────────────
    void        SetParameter(int index, float value);
    float       GetParameter(int index);
    const char* GetParameterName(int index);
    std::string GetParameterDisplay(int index);

    // ───────────────────────────────────────────────
    // INSTRUMENTE
    // ───────────────────────────────────────────────
    void SetInstrument(int id);
    int  GetInstrument();
    void GetInstrumentName(char* name);
    std::string GetInstrumentNameByID(int id);
    int  GetInstrumentCount();

    // ───────────────────────────────────────────────
    // PRESETS
    // ───────────────────────────────────────────────
    int                  GetNumPresets();
    const PresetInfo*    GetPreset(int index);
    void                 LoadPreset(int index);
    void                 PrevPreset();        // Convenience: Load previous
    void                 NextPreset();        // Convenience: Load next
    int                  SaveUserPreset(const std::string& name,
                                        const std::string& category = "User",
                                        const std::string& description = "");

    // ───────────────────────────────────────────────
    // MASTER
    // ───────────────────────────────────────────────
    void SetMasterVolume(float volNorm); // 0..1
    int  GetMasterVolume127() const;

    // ───────────────────────────────────────────────
    // MIDI SETTINGS
    // ───────────────────────────────────────────────
    int getMidiSetting(int id) const;
    void setMidiSetting(int id, int value);

    // ───────────────────────────────────────────────
    // SYSTEM INFO
    // ───────────────────────────────────────────────
    const char*       GetAudioBackendName() const;
    const char*       GetAudioDeviceName() const;
    unsigned          GetSampleRate() const;
    unsigned          GetPolyphony() const;
    unsigned          GetChunkSize() const;
    bool              IsStereoSwapped() const;
    bool              IsUSBDeviceMode() const;
    unsigned          GetUSBGadgetPin() const;
    const char*       GetFirmwareVersion() const;

    // ─────────────
    // RAW ACCESS
    // ─────────────
    COPMEmuAdapter*   GetOPMEmuAdapter() const { return const_cast<COPMEmuAdapter*>(&m_piano); }

    // ─────────────
    // SOUND
    // ─────────────
    unsigned GenerateAudio(float* outL, float* outR, unsigned nFrames);

    // Test tone: pure 440Hz sine (bypasses synth engine).
    // Phase wird als Radians in [0, 2π) akkumuliert, damit sinf() auch
    // nach langer Laufzeit präzise bleibt (sample-zählender Counter
    // hätte nach wenigen Minuten Float-Präzisionsverlust).
    static const unsigned TestToneFreq = 440;
    float m_testTonePhase = 0.0f;

    // ───────────────────────────────────────────────
    // MULTICORE AUDIO DOUBLE-BUFFER (Core 1 -> Core 0 DMA)
    // ───────────────────────────────────────────────
#ifdef ARM_ALLOW_MULTI_CORE
    // AudioBufferSlots: Anzahl Audio-Slots zwischen Core 1 (Producer)
    // und Core 0 / DMA-IRQ (Consumer).
    //
    // Worst-Case-Latenz von NoteOn bis Ton ≈ (Slots + 2) × ChunkTime,
    // weil die DMA selbst noch 2 Buffer queued hat (Circle-intern).
    //
    //   8 Slots, 1024er Chunks @ 48 kHz: ~107 ms (zu hoch für E-Piano)
    //   2 Slots, 1024er Chunks @ 48 kHz:  ~43 ms (Minimum, jitterarm)
    //
    // Bei 2 Slots muss processBlock() konsistent unter ChunkTime
    // bleiben (~10.7 ms). Das ist auf einem Pi Zero 2 W bei 16
    // Stimmen problemlos drin. Falls bei sehr schwerer Effektkette
    // doch Underruns auftreten ("Repeat-Last"-Knack alle paar
    // Sekunden), auf 4 erhöhen — Wert MUSS Potenz von 2 sein
    // (static_assert in ringbuffer.h).
    static const unsigned AudioBufferSlots  = 2;
    static const unsigned AudioBufferFrames = 4096;

    // Core 1: Produziert Audio-Blöcke.
    // Rückgabewert: true = Slot gefüllt, false = Buffer war voll.
    // Bei false sollte der Caller kurz warten, statt sofort wieder
    // anzufragen (vermeidet Hot-Spin und das damit verbundene
    // Heißlaufen von Core 1 auf dem Pi Zero 2 W).
    bool FillAudioSlot();

    // Core 0 / DMA-IRQ: Holt fertige Audio-Blöcke
    // Gibt true zurück, wenn ein Slot verfügbar war.
    // outL/outR müssen mindestens AudioBufferFrames gross sein.
    bool GetAudioChunk(float* outL, float* outR, unsigned& outFrames);

    // Core 3: Verarbeitet Deferred Work (SysEx, Preset-Load)
    void ProcessDeferredWork();
#endif

private:
    // Helpers
    int applyVelocityCurve(int vel) const;
    int applyTranspose(int note) const;

private:
    // Config / system
    CConfig*          m_pConfig;
    CInterruptSystem* m_pInterrupt;
    CGPIOManager*     m_pGPIOManager;
    CI2CMaster*       m_pI2CMaster;
    CSPIMaster*       m_pSPIMaster;
    FATFS*            m_pFileSystem;

    // Engine
    COPMEmuAdapter   m_piano;
    IFileSystem*      m_pCircleFS;
    float             m_sampleRate;
    int               m_maxPolyphony;

    // Audio
    CSoundBaseDevice* m_pSoundDevice;
    unsigned          m_queueFrames;

public:
    bool              m_bChannelsSwapped;
    float             m_masterVolume; // 0..1

    // Pause flag: set before preset/instrument loading (which involves SD I/O
    // and can't run with IRQs disabled). Checked by GenerateAudio (DMA IRQ).
    // When true, DMA outputs silence and skips processBlock().
    std::atomic<bool> m_audioPaused;

private:
    // MIDI SETTINGS
    int               m_MidiChannel;
    int               m_VelocityCurve;
    int               m_Transpose;
    bool              m_LocalControl;
    bool              m_MidiThru;
    bool              m_ProgramChange;
    int               m_PitchBendRange;

    // MIDI devices
    static const unsigned MaxUSBMIDIDevices = 4;

    CMicroDX21USBMIDIDevice*    m_usbMidi[MaxUSBMIDIDevices];
    CMicroDX21SerialMIDIDevice* m_serialMidi;
    CMicroDX21PCKeyboard*       m_pcKeyboard;

    // Deferred SysEx queue (moves heavy processing out of IRQ)
    // Thread-safe für Single-Core (EnterCritical) und Multicore (atomare Indizes)
    static const unsigned SysExQueueSize = 2;
    static const unsigned SysExMaxLen    = 65536;
    struct SysExQueueEntry {
        u8 data[SysExMaxLen];
        size_t len;
        CMicroDX21MIDIDevice* source;
    };
    SysExQueueEntry m_sysExQueue[SysExQueueSize];
    std::atomic<unsigned> m_sysExHead{0};
    std::atomic<unsigned> m_sysExTail{0};

    void ProcessDeferredSysEx();

#ifdef ARM_ALLOW_MULTI_CORE
    // ───────────────────────────────────────────────
    // MULTICORE: Audio Double-Buffer (Core 1 -> Core 0)
    // ───────────────────────────────────────────────
    LockfreeAudioBuffer<AudioBufferSlots, AudioBufferFrames> m_audioBuffer;

    // Repeat-Last: Wenn kein Ready-Slot verfügbar, wird der letzte
    // Block erneut ausgegeben (verhindert Dropouts).
    float m_lastOutL[AudioBufferFrames];
    float m_lastOutR[AudioBufferFrames];
    unsigned m_lastFrames = 0;
    bool m_haveLastBlock = false;
#endif
};

#endif