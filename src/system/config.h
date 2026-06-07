//
// config.h – microDX21 application configuration
//
// Reads /boot/microdx21.ini (via FatFS) at boot. The CConfig class is
// the single source of truth for every tweakable knob the user might
// want to change on the SD card: audio backend, sample rate, MIDI
// channel filter, master volume, OLED type
// and pin assignment, rotary encoder GPIO.
//

#ifndef _config_h
#define _config_h

#include <fatfs/ff.h>
#include <Properties/propertiesfatfsfile.h>
#include <string>
#include "displayconfig.h"

class CConfig
{
public:
    static const unsigned MaxNotes     = 88;  // hardware cap (YMF2164 OP chips)
    static const unsigned DefaultNotes = 64;  // safe default for a Pi 3
    static const unsigned MaxChunkSize = 4096;

public:
    CConfig(FATFS* pFileSystem);
    ~CConfig();

    void Load();

    // ───────────────────────────────────────────────
    // ENGINE / AUDIO
    // ───────────────────────────────────────────────
    unsigned    GetPolyphony() const;
    const char* GetSoundDevice() const;      // "pwm" or "i2s"
    unsigned    GetSampleRate() const;
    unsigned    GetChunkSize() const;
    unsigned    GetDACI2CAddress() const;    // 0 = on-board PWM, else PCM5102A I2C addr
    bool        GetChannelsSwapped() const;
    bool        GetTestToneEnabled() const;

    // ───────────────────────────────────────────────
    // MIDI
    // ───────────────────────────────────────────────
    unsigned    GetMIDIBaudRate() const;
    bool        GetMIDIThruEnabled() const;
    // 0 = Omni (accept all channels), 1..16 = specific channel
    unsigned    GetMIDIChannel() const;

    // ───────────────────────────────────────────────
    // MASTER VOLUME
    // ───────────────────────────────────────────────
    unsigned    GetMasterVolume() const;     // 0..127

    // ───────────────────────────────────────────────
    // DISPLAY
    // ───────────────────────────────────────────────
    DisplayConfig GetDisplayConfig() const;

private:
    CPropertiesFatFsFile m_Properties;

    // Engine / Audio
    unsigned    m_nPolyphony;
    std::string m_SoundDevice;
    unsigned    m_nSampleRate;
    unsigned    m_nChunkSize;
    unsigned    m_nDACI2CAddress;
    bool        m_bChannelsSwapped;
    bool        m_bTestToneEnabled;


    // MIDI
    unsigned    m_nMIDIBaudRate;
    bool        m_bMIDIThruEnabled;
    unsigned    m_nMIDIChannel;       // 0 = Omni, 1..16 = specific

    // Master Volume
    unsigned    m_nMasterVolume;

    // Display
    DisplayConfig m_DisplayConfig;
};

#endif
