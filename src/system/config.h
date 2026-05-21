//
// config.h – minimal für VelvetKeys
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

    static const unsigned MaxNotes     = 88; // see NVOICES in vkpiano.h
    static const unsigned DefaultNotes = 64;

    static const unsigned MaxChunkSize = 4096;

public:
    CConfig(FATFS* pFileSystem);
    ~CConfig();

    void Load();

    // ───────────────────────────────────────────────
    // ENGINE / AUDIO
    // ───────────────────────────────────────────────
    unsigned    GetPolyphony() const;
    const char* GetSoundDevice() const;      // "pwm", "i2s", "hdmi"
    unsigned    GetSampleRate() const;
    unsigned    GetChunkSize() const;
    unsigned    GetDACI2CAddress() const;    // 0 = auto
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

    // USB
    bool        m_bUSBGadget;
    unsigned    m_nUSBGadgetPin;
    bool        m_bUSBGadgetMode;

public:
    bool IsUSBGadget() const
    {
#if RASPPI >= 5
        return false; // RPi 5 hat keinen USB-Gadget-Modus
#else
        return m_bUSBGadget;
#endif
    }
    unsigned GetUSBGadgetPin() const { return m_nUSBGadgetPin; }
    bool IsUSBGadgetModeComposite() const { return m_bUSBGadgetMode; }

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
