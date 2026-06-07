//
// microdx21.cpp
//
// Single-core audio: Core 0 runs DMA interrupt (GenerateAudio) +
// main loop (MIDI, deferred preset load).
//
// Race protection strategy:
// - Short synth writes (NoteOn, SetParameter, etc.): EnterCritical()/LeaveCritical()
//   briefly disables IRQs so DMA interrupt cannot call processBlock() mid-write.
// - Long operations with SD I/O (LoadPreset, SetInstrument): m_audioPaused flag
//   makes DMA output silence and skip processBlock() for the duration.
//

#include "microdx21.h"

#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/gpiopin.h>
#include <circle/synchronize.h>
#include "opm/io/fatfs_filesystem.h"
#include <string.h>
#include <math.h>

LOGMODULE("microdx21");

CMicroDX21::CMicroDX21(CConfig*          pConfig,
                         CInterruptSystem* pInterrupt,
                         CGPIOManager*     pGPIOManager,
                         CI2CMaster*       pI2CMaster,
                         CSPIMaster*       pSPIMaster,
                         FATFS*            pFileSystem)
:
 m_pConfig(pConfig)
, m_pInterrupt(pInterrupt)
, m_pGPIOManager(pGPIOManager)
, m_pI2CMaster(pI2CMaster)
, m_pSPIMaster(pSPIMaster)
, m_pFileSystem(pFileSystem)
, m_piano(pConfig->GetSampleRate(), pConfig->GetPolyphony())
, m_pCircleFS(nullptr)
, m_sampleRate(pConfig->GetSampleRate())
, m_maxPolyphony(pConfig->GetPolyphony())
, m_pSoundDevice(nullptr)
, m_queueFrames(pConfig->GetChunkSize() / 2)
, m_bChannelsSwapped(pConfig->GetChannelsSwapped())
, m_masterVolume(1.0f)
, m_audioPaused(false)
{
    for (unsigned i = 0; i < MaxUSBMIDIDevices; i++)
        m_usbMidi[i] = nullptr;

    m_serialMidi = nullptr;
    m_pcKeyboard = nullptr;

    // MIDI defaults — m_MidiChannel uses 0=Omni, 1..16=specific.
    // Real value is loaded from microdx21.ini in Initialize().
    m_MidiChannel         = 0;        // Omni until config is parsed
    m_VelocityCurve       = 0;
    m_Transpose           = 0;
    m_LocalControl        = true;
    m_MidiThru            = false;
    m_ProgramChange       = true;
    m_PitchBendRange      = 2;
}

CMicroDX21::~CMicroDX21()
{
    for (unsigned i = 0; i < MaxUSBMIDIDevices; i++)
        delete m_usbMidi[i];

    delete m_serialMidi;
    delete m_pcKeyboard;

    delete m_pCircleFS;
    delete m_pSoundDevice;
}

bool CMicroDX21::Initialize()
{
    LOGNOTE("VelvetKeys: Initialize()");

    LOGNOTE("VelvetKeys: starting FatFsFileSystem");
    m_pCircleFS = new FatFsFileSystem(m_pFileSystem, "SD:");

    LOGNOTE("VelvetKeys: setFileSystem()");
    m_piano.setFileSystem(m_pCircleFS);

    LOGNOTE("VelvetKeys: calling m_piano.init(\"presets\")");
    if (!m_piano.init("presets"))
    {
        LOGERR("COPMEmu init failed");
        return false;
    }
    LOGNOTE("VelvetKeys: m_piano.init() completed successfully");

    LOGNOTE("VelvetKeys: setSysexOutCallback()");
    m_piano.setSysexOutCallback(
          [](void* user, const uint8_t* data, size_t len)
          {
            CMicroDX21* self = static_cast<CMicroDX21*>(user);
            if (!self || !data || len == 0)
                return;
            self->ForwardMIDI(data, len, nullptr);
          },
        this);

    // Master Volume aus Config
    float mv = (float)m_pConfig->GetMasterVolume() / 127.0f;
    SetMasterVolume(mv);

    // MIDI channel filter aus Config (0=Omni, 1..16=specific)
    m_MidiChannel = (int)m_pConfig->GetMIDIChannel();
    if (m_MidiChannel < 0 || m_MidiChannel > 16)
        m_MidiChannel = 0;
    LOGNOTE("MIDI channel filter: %s",
            m_MidiChannel == 0 ? "Omni"
                               : (std::to_string(m_MidiChannel)).c_str());

    // ───────────────────────────────────────────────
    // MIDI DEVICES
    // ───────────────────────────────────────────────
    LOGNOTE("VelvetKeys: InitMidi");

    // Host mode only: up to 4 USB-MIDI devices (Plug & Play).
    // The USB-MIDI Gadget is now handled by the external RP2350
    // "Comms" processor (pico-midi-adapter) over UART.
    for (unsigned i = 0; i < MaxUSBMIDIDevices; i++)
        m_usbMidi[i] = new CMicroDX21USBMIDIDevice(this, m_pConfig, i);

    m_serialMidi = new CMicroDX21SerialMIDIDevice(this, m_pInterrupt, m_pConfig);
    if (!m_serialMidi->Initialize())
    {
        LOGERR("Serial MIDI Device init failed (probably used by console) - disabling");
        delete m_serialMidi;
        m_serialMidi = nullptr;
    }
    else
    {
        LOGNOTE("VelvetKeys: Serial MIDI Device initialized successfully");
    }

    m_pcKeyboard = new CMicroDX21PCKeyboard(this, m_pConfig);

    return true;
}

void CMicroDX21::Process(bool plugAndPlayUpdated)
{
    for (unsigned i = 0; i < MaxUSBMIDIDevices; i++)
        if (m_usbMidi[i])
            m_usbMidi[i]->Process(plugAndPlayUpdated);

    if (m_serialMidi)
        m_serialMidi->Process();

    if (m_pcKeyboard)
        m_pcKeyboard->Process(plugAndPlayUpdated);

#ifndef ARM_ALLOW_MULTI_CORE
    // Single-Core: Deferred SysEx wird hier verarbeitet
    ProcessDeferredSysEx();
#endif

    CScheduler::Get()->Yield();
}

// ═══════════════════════════════════════════════════
// AUDIO (DMA interrupt on Core 0)
// ═══════════════════════════════════════════════════

unsigned CMicroDX21::GenerateAudio(float* outL, float* outR, unsigned nFrames)
{
    if (nFrames > 4096)
        nFrames = 4096;

    // Test tone: 440Hz sine to verify I2S audio path independently
    if (m_pConfig->GetTestToneEnabled())
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float phaseInc = kTwoPi * (float)TestToneFreq / m_sampleRate;
        float phase = m_testTonePhase;
        for (unsigned i = 0; i < nFrames; ++i)
        {
            float sample = 0.5f * sinf(phase);
            outL[i] = sample * m_masterVolume;
            outR[i] = sample * m_masterVolume;
            phase += phaseInc;
            if (phase >= kTwoPi) phase -= kTwoPi;
        }
        m_testTonePhase = phase;
        return nFrames;
    }

    memset(outL, 0, nFrames * sizeof(float));
    memset(outR, 0, nFrames * sizeof(float));

    if (!m_audioPaused.load(std::memory_order_acquire))
    {
        m_piano.raw()->processBlock(outL, outR, (int)nFrames);
    }

    for (unsigned i = 0; i < nFrames; ++i)
    {
        float l = outL[i] * m_masterVolume;
        float r = outR[i] * m_masterVolume;

        if (l > 1.0f)  l = 1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r > 1.0f)  r = 1.0f;
        if (r < -1.0f) r = -1.0f;

        outL[i] = l;
        outR[i] = r;
    }

    return nFrames;
}

#ifdef ARM_ALLOW_MULTI_CORE
// ═══════════════════════════════════════════════════
// MULTICORE AUDIO DOUBLE-BUFFER
// Core 1: FillAudioSlot()  – produziert Audio-Blöcke
// Core 0: GetAudioChunk()  – konsumiert Audio-Blöcke (DMA-IRQ)
// ═══════════════════════════════════════════════════

bool CMicroDX21::FillAudioSlot()
{
    // Suche einen Empty-Slot und markiere ihn als Filling
    auto* slot = m_audioBuffer.AcquireEmptySlot();
    if (!slot)
        return false; // Alle Slots Ready — Caller soll kurz warten

    unsigned nFrames = m_queueFrames;
    assert(nFrames > 0 && nFrames <= AudioBufferFrames);

    // Test tone: 440Hz sine to verify I2S audio path independently
    if (m_pConfig->GetTestToneEnabled())
    {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float phaseInc = kTwoPi * (float)TestToneFreq / m_sampleRate;
        float phase = m_testTonePhase;
        for (unsigned i = 0; i < nFrames; ++i)
        {
            float sample = 0.5f * sinf(phase);
            slot->outL[i] = sample;
            slot->outR[i] = sample;
            phase += phaseInc;
            if (phase >= kTwoPi) phase -= kTwoPi;
        }
        m_testTonePhase = phase;
    }
    else if (!m_audioPaused.load(std::memory_order_acquire))
    {
        m_piano.raw()->processBlock(slot->outL, slot->outR, (int)nFrames);
    }
    else
    {
        memset(slot->outL, 0, nFrames * sizeof(float));
        memset(slot->outR, 0, nFrames * sizeof(float));
    }

    // Master Volume + Clamping
    for (unsigned i = 0; i < nFrames; ++i)
    {
        float l = slot->outL[i] * m_masterVolume;
        float r = slot->outR[i] * m_masterVolume;

        if (l > 1.0f)  l = 1.0f;
        if (l < -1.0f) l = -1.0f;
        if (r > 1.0f)  r = 1.0f;
        if (r < -1.0f) r = -1.0f;

        slot->outL[i] = l;
        slot->outR[i] = r;
    }

    slot->nFrames = nFrames;

    // Cache für Repeat-Last
    memcpy(m_lastOutL, slot->outL, nFrames * sizeof(float));
    memcpy(m_lastOutR, slot->outR, nFrames * sizeof(float));
    m_lastFrames = nFrames;
    m_haveLastBlock = true;

    // Jetzt erst als Ready markieren (memory_order_release stellt sicher,
    // dass alle vorherigen Writes sichtbar sind, bevor der Slot konsumiert wird)
    m_audioBuffer.CommitSlot(slot);
    return true;
}

bool CMicroDX21::GetAudioChunk(float* outL, float* outR, unsigned& outFrames)
{
    auto* slot = m_audioBuffer.AcquireReadySlot();
    if (slot)
    {
        unsigned n = slot->nFrames;
        if (n > m_queueFrames)
            n = m_queueFrames;

        memcpy(outL, slot->outL, n * sizeof(float));
        memcpy(outR, slot->outR, n * sizeof(float));
        outFrames = n;

        m_audioBuffer.ReleaseSlot(slot);
        return true;
    }

    // Kein Ready-Slot verfügbar: Repeat-Last oder Stille
    if (m_haveLastBlock)
    {
        memcpy(outL, m_lastOutL, m_lastFrames * sizeof(float));
        memcpy(outR, m_lastOutR, m_lastFrames * sizeof(float));
        outFrames = m_lastFrames;
    }
    else
    {
        memset(outL, 0, m_queueFrames * sizeof(float));
        memset(outR, 0, m_queueFrames * sizeof(float));
        outFrames = m_queueFrames;
    }
    return false;
}

// ═══════════════════════════════════════════════════
// MULTICORE DEFERRED WORK (Core 3)
// ═══════════════════════════════════════════════════

void CMicroDX21::ProcessDeferredWork()
{
    // SysEx-Queue verarbeiten
    ProcessDeferredSysEx();
}
#endif // ARM_ALLOW_MULTI_CORE

// ───────────────────────────────────────────────
// MIDI / ENGINE
// EnterCritical() prevents DMA IRQ from calling processBlock()
// while we modify synth state (voices, parameters, etc.).
// The critical section is very short (a few microseconds).
// ───────────────────────────────────────────────

void CMicroDX21::NoteOn(int32_t note, int32_t velocity)
{
    EnterCritical();
    m_piano.noteOn(note, velocity);
    LeaveCritical();
}

void CMicroDX21::NoteOff(int32_t note)
{
    EnterCritical();
    m_piano.noteOff(note);
    LeaveCritical();
}

void CMicroDX21::SendMidiCmd(uint8_t cmd, uint8_t data1, uint8_t data2)
{
    EnterCritical();
    m_piano.sendMidiCmd(cmd, data1, data2);
    LeaveCritical();
}

// Channel filter — consulted by CMicroDX21MIDIDevice::HandleMIDI before
// dispatching channel-voice messages to the synth engine.  See header.
bool CMicroDX21::ShouldAcceptChannel(u8 channel0Based) const
{
    if (m_MidiChannel <= 0)
        return true;                                    // Omni
    return ((unsigned)m_MidiChannel - 1) == (unsigned)channel0Based;
}

void CMicroDX21::ForwardMIDI(const u8* msg, size_t len, CMicroDX21MIDIDevice* source)
{
    for (unsigned i = 0; i < MaxUSBMIDIDevices; i++)
    {
        if (m_usbMidi[i] && m_usbMidi[i] != source)
            m_usbMidi[i]->Send(msg, len);
    }

    if (m_serialMidi && m_serialMidi != source)
        m_serialMidi->Send(msg, len);

    if (m_pcKeyboard && m_pcKeyboard != source)
        m_pcKeyboard->Send(msg, len);
}

void CMicroDX21::HandleSysExFromDevice(const u8* msg, size_t len, CMicroDX21MIDIDevice* source)
{
    if (!msg || len < 2)
        return;

    if (msg[0] != 0xF0 || msg[len - 1] != 0xF7)
        return;

    if (len > SysExMaxLen) {
        LOGWARN("SysEx too large (%zu > %u), dropping", len, SysExMaxLen);
        return;
    }

    // Atomares Enqueue (thread-safe für Single-Core + Multicore)
    unsigned head = m_sysExHead.load(std::memory_order_relaxed);
    unsigned nextHead = (head + 1) % SysExQueueSize;
    if (nextHead == m_sysExTail.load(std::memory_order_acquire)) {
        LOGWARN("SysEx queue full, dropping message");
        return;
    }

    SysExQueueEntry& entry = m_sysExQueue[head];
    memcpy(entry.data, msg, len);
    entry.len = len;
    entry.source = source;
    m_sysExHead.store(nextHead, std::memory_order_release);
}

void CMicroDX21::ProcessDeferredSysEx()
{
    while (true)
    {
        unsigned tail = m_sysExTail.load(std::memory_order_relaxed);
        if (m_sysExHead.load(std::memory_order_acquire) == tail) {
            break; // leer
        }
        SysExQueueEntry entry = m_sysExQueue[tail];
        m_sysExTail.store((tail + 1) % SysExQueueSize, std::memory_order_release);

        // Heavy processing (file I/O, allocations) happens in main loop, not IRQ
        m_audioPaused.store(true, std::memory_order_release);
        m_piano.handleSysexMessage(entry.data, entry.len);
        m_audioPaused.store(false, std::memory_order_release);

        ForwardMIDI(entry.data, entry.len, entry.source);
    }
}

// ───────────────────────────────────────────────
// PARAMETERS
// ───────────────────────────────────────────────

void CMicroDX21::SetParameter(int index, float value)
{
    EnterCritical();
    m_piano.setParameter(index, value);
    LeaveCritical();
}

float CMicroDX21::GetParameter(int index)
{
    // Read-only; a torn read of a single float is benign on ARM (aligned 4-byte read)
    return m_piano.getParameter(index);
}

const char* CMicroDX21::GetParameterName(int index)
{
    return m_piano.getParameterName(index);
}

std::string CMicroDX21::GetParameterDisplay(int index)
{
    return m_piano.getParameterDisplay(index);
}

// ───────────────────────────────────────────────
// INSTRUMENTE
// setInstrument() involves SD I/O (selectPreset) — can't disable IRQs.
// Uses m_audioPaused like LoadPreset: pause audio, defer the load.
// ───────────────────────────────────────────────

void CMicroDX21::SetInstrument(int id)
{
    if (id < 0) return;

    // Prüfen, ob der Index gültig ist (Grenzen der Preset-Liste)
    int numPresets = m_piano.getNumPresets();
    if (id >= numPresets) {
        CLogger::Get()->Write("microdx21", LogError, "SetInstrument: index %d out of range (count=%d)", id, numPresets);
        return;
    }

    m_audioPaused.store(true, std::memory_order_release);
    m_piano.loadPreset(id);
    m_audioPaused.store(false, std::memory_order_release);
}

int CMicroDX21::GetInstrument()
{
    return m_piano.getInstrument();
}

void CMicroDX21::GetInstrumentName(char* name)
{
    if (!name) return;
    const PresetInfo* p = m_piano.getCurrentPresetInfo();
    if (!p) {
        name[0] = '\0';
        return;
    }
    std::snprintf(name, 64, "%s", p->name.c_str());
}

std::string CMicroDX21::GetInstrumentNameByID(int id)
{
    const PresetInfo* preset = m_piano.getPreset(id);
    if (preset)
        return preset->name;
    return "";
}

int CMicroDX21::GetInstrumentCount()
{
    return m_piano.getInstrumentCount();
}

// ───────────────────────────────────────────────
// PRESETS
// LoadPreset involves SD I/O — can't disable IRQs.
// Uses m_audioPaused: DMA outputs silence, processBlock() is skipped.
// Actual load is deferred to Process() where IRQs are enabled.
// ───────────────────────────────────────────────

int CMicroDX21::GetNumPresets()
{
    return m_piano.getNumPresets();
}

const PresetInfo* CMicroDX21::GetPreset(int index)
{
    return m_piano.getPreset(index);
}

void CMicroDX21::LoadPreset(int index)
{
    if (index < 0) return;

    LOGNOTE("VelvetKeys: LoadPreset() start");

    // Prüfen, ob der Index gültig ist (Grenzen der Preset-Liste)
    int numPresets = m_piano.getNumPresets();
    if (index >= numPresets) {
        CLogger::Get()->Write("microdx21", LogError, "LoadPreset: index %d out of range (count=%d)", index, numPresets);
        return;
    }

    // Audio pausieren
    m_audioPaused.store(true, std::memory_order_release);

    LOGNOTE("VelvetKeys: m_piano.loadPreset(index)");
    // Preset laden (blockiert bis fertig)
    m_piano.loadPreset(index);

    m_audioPaused.store(false, std::memory_order_release);
    LOGNOTE("VelvetKeys: LoadPreset() end");
}

int CMicroDX21::SaveUserPreset(const std::string& name,
                                const std::string& category,
                                const std::string& description)
{
    return m_piano.saveUserPreset(name, category, description);
}

void CMicroDX21::PrevPreset()
{
    int current = GetInstrument();
    int numPresets = GetInstrumentCount();
    int next = (current - 1 + numPresets) % numPresets;
    LoadPreset(next);
}

void CMicroDX21::NextPreset()
{
    int current = GetInstrument();
    int numPresets = GetInstrumentCount();
    int next = (current + 1) % numPresets;
    LoadPreset(next);
}

// ───────────────────────────────────────────────
// MASTER VOLUME
// ───────────────────────────────────────────────

void CMicroDX21::SetMasterVolume(float volNorm)
{
    if (volNorm < 0.0f) volNorm = 0.0f;
    if (volNorm > 1.0f) volNorm = 1.0f;
    m_masterVolume = volNorm;
}

int CMicroDX21::GetMasterVolume127() const
{
    if (m_masterVolume <= 0.0f) return 0;
    if (m_masterVolume >= 1.0f) return 127;
    return (int)(sqrtf(m_masterVolume) * 127.0f);
}

// ───────────────────────────────────────────────
// MIDI SETTINGS
// ───────────────────────────────────────────────

int CMicroDX21::getMidiSetting(int id) const
{
    switch (id)
    {
        case kMidiChannel:        return m_MidiChannel;
        case kMidiVelocityCurve:  return m_VelocityCurve;
        case kMidiTranspose:      return m_Transpose;
        case kMidiLocalControl:   return m_LocalControl;
        case kMidiThru:           return m_MidiThru;
        case kMidiProgramChange:  return m_ProgramChange;
        case kMidiPitchBendRange: return m_PitchBendRange;
    }
    return 0;
}

void CMicroDX21::setMidiSetting(int id, int value)
{
    switch (id)
    {
        case kMidiChannel:
            // 0 = Omni, 1..16 = specific channel
            m_MidiChannel = std::clamp(value, 0, 16);
            break;

        case kMidiVelocityCurve:
            m_VelocityCurve = std::clamp(value, 0, 2);
            break;

        case kMidiTranspose:
            m_Transpose = std::clamp(value, -12, 12);
            break;

        case kMidiLocalControl:
            m_LocalControl = (value != 0);
            break;

        case kMidiThru:
            m_MidiThru = (value != 0);
            break;

        case kMidiProgramChange:
            m_ProgramChange = (value != 0);
            break;

        case kMidiPitchBendRange:
            m_PitchBendRange = std::clamp(value, 0, 12);
            break;
    }
}

int CMicroDX21::applyVelocityCurve(int vel) const
{
    switch (m_VelocityCurve)
    {
        case 1: // Soft
            return (int)(vel * 0.7f);

        case 2: // Hard
            return std::min(127, (int)(vel * 1.3f));

        default: // Linear
            return vel;
    }
}

int CMicroDX21::applyTranspose(int note) const
{
    int n = note + m_Transpose;
    return std::clamp(n, 0, 127);
}

const char* CMicroDX21::GetAudioBackendName() const
{
    return m_pConfig->GetSoundDevice();
}

const char* CMicroDX21::GetAudioDeviceName() const
{
    return "";
}

unsigned CMicroDX21::GetSampleRate() const
{
    return m_sampleRate;
}

unsigned CMicroDX21::GetPolyphony() const
{
    return m_maxPolyphony;
}

unsigned CMicroDX21::GetChunkSize() const
{
    return m_pConfig->GetChunkSize();
}

bool CMicroDX21::IsStereoSwapped() const
{
    return m_bChannelsSwapped;
}

const char* CMicroDX21::GetFirmwareVersion() const
{
    return "1.0.0";
}