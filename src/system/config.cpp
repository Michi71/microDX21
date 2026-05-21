//
// config.cpp – minimal für VelvetKeys
//

#include "config.h"

CConfig::CConfig(FATFS* pFileSystem)
: m_Properties("velvetkeys.ini", pFileSystem)
, m_bUSBGadget(false)
, m_nUSBGadgetPin(0)
, m_bUSBGadgetMode(false)
{
}

CConfig::~CConfig()
{
}

void CConfig::Load()
{
    m_Properties.Load();

    // ───────────────────────────────────────────────
    // Hilfsfunktion: trim
    // ───────────────────────────────────────────────
    auto trim = [](std::string &s)
    {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(0, 1);
    };

    // ───────────────────────────────────────────────
    // Polyphony
    //
    // MaxNotes is the absolute hardware/algorithm cap (must mirror NVOICES
    // in vkpiano.h).  Any INI value above MaxNotes is clamped down to
    // MaxNotes — we don't fall back to the default, because a user who
    // wrote "Polyphony=128" most likely wants "as many voices as possible",
    // not the conservative default.
    //
    // Sub-1 values are nonsensical; clamp to 1.
    // ───────────────────────────────────────────────
    m_nPolyphony = m_Properties.GetNumber("Polyphony", DefaultNotes);
    if (m_nPolyphony > MaxNotes)
        m_nPolyphony = MaxNotes;
    if (m_nPolyphony < 1)
        m_nPolyphony = 1;

    // ───────────────────────────────────────────────
    // Audio
    // ───────────────────────────────────────────────
    m_SoundDevice      = m_Properties.GetString("SoundDevice", "pwm");
    trim(m_SoundDevice);

    m_nSampleRate      = m_Properties.GetNumber("SampleRate", 48000);
    m_nChunkSize       = m_Properties.GetNumber("ChunkSize", 1024);
    m_nDACI2CAddress   = m_Properties.GetNumber("DACI2CAddress", 0);
    m_bChannelsSwapped = m_Properties.GetNumber("ChannelsSwapped", 0) != 0;
    m_bTestToneEnabled = m_Properties.GetNumber("TestTone", 0) != 0;

    // ───────────────────────────────────────────────
    // MIDI
    // ───────────────────────────────────────────────
    m_nMIDIBaudRate    = m_Properties.GetNumber("MIDIBaudRate", 31250);
    m_bMIDIThruEnabled = m_Properties.GetNumber("MIDIThruEnabled", 0) != 0;

    // MIDI channel filter:
    //   0      = Omni (accept all channels)
    //   1..16  = accept only this channel
    // Out-of-range values fall back to 0 (Omni) so a typo never hard-mutes
    // the synth.
    m_nMIDIChannel = m_Properties.GetNumber("MidiChannel", 0);
    if (m_nMIDIChannel > 16)
        m_nMIDIChannel = 0;

    // ───────────────────────────────────────────────
    // USB
    // ───────────────────────────────────────────────
    m_bUSBGadget = m_Properties.GetNumber("USBGadget", 0) != 0;
    m_nUSBGadgetPin = m_Properties.GetNumber("USBGadgetPin", 0);
    m_bUSBGadgetMode = m_Properties.GetNumber("USBGadgetMode", 0) != 0;

    // ───────────────────────────────────────────────
    // Master Volume
    // ───────────────────────────────────────────────
    m_nMasterVolume = m_Properties.GetNumber("MasterVolume", 100);

    // ───────────────────────────────────────────────
    // DISPLAY
    // ───────────────────────────────────────────────
    m_DisplayConfig.enabled =
        m_Properties.GetNumber("DisplayEnabled", 1) != 0;

    // Controller
    std::string ctrl = m_Properties.GetString("DisplayType", "ssd1306");
    trim(ctrl);

    if (ctrl == "ssd1306")      m_DisplayConfig.controller = DisplayConfig::Controller::SSD1306;
    else if (ctrl == "ssd1305") m_DisplayConfig.controller = DisplayConfig::Controller::SSD1305;
    else if (ctrl == "sh1106")  m_DisplayConfig.controller = DisplayConfig::Controller::SH1106;
    else if (ctrl == "st7789")  m_DisplayConfig.controller = DisplayConfig::Controller::ST7789;
    else if (ctrl == "hdmi")    m_DisplayConfig.controller = DisplayConfig::Controller::HDMI;
    else if (ctrl == "epd2in13v4") m_DisplayConfig.controller = DisplayConfig::Controller::EPD2IN13V4;
    else                        m_DisplayConfig.controller = DisplayConfig::Controller::Custom;

    // Bus
    std::string bus = m_Properties.GetString("DisplayBus", "i2c");
    trim(bus);

    if (bus == "i2c")           m_DisplayConfig.bus = DisplayConfig::Bus::I2C;
    else if (bus == "spi")      m_DisplayConfig.bus = DisplayConfig::Bus::SPI;
    else if (bus == "hdmi")     m_DisplayConfig.bus = DisplayConfig::Bus::HDMI;
    else                        m_DisplayConfig.bus = DisplayConfig::Bus::None;

    // Resolution
    m_DisplayConfig.resolution.width  = m_Properties.GetNumber("DisplayWidth", 128);
    m_DisplayConfig.resolution.height = m_Properties.GetNumber("DisplayHeight", 64);

    // I2C
    m_DisplayConfig.i2cAddress = m_Properties.GetNumber("DisplayI2CAddress", 0x3C);

    // SPI
    m_DisplayConfig.spiBus   = m_Properties.GetNumber("DisplaySPIBus", 0);
    m_DisplayConfig.spiMode  = m_Properties.GetNumber("DisplaySPIMode", 0);
    m_DisplayConfig.pinDC    = m_Properties.GetNumber("DisplaySPIDCPin", 0);
    m_DisplayConfig.pinReset = m_Properties.GetNumber("DisplaySPIResetPin", 0);
    m_DisplayConfig.spiSpeed = m_Properties.GetNumber("DisplaySPISpeed", 8000000);

    // Rotation
    m_DisplayConfig.rotation = m_Properties.GetNumber("DisplayRotation", 0);

    // Encoder
    m_DisplayConfig.encPinA   = m_Properties.GetNumber("EncoderPinA", 10);
    m_DisplayConfig.encPinB   = m_Properties.GetNumber("EncoderPinB", 9);
    m_DisplayConfig.encPinBtn = m_Properties.GetNumber("EncoderPinBtn", 11);

    // EPD (für EPD2in13V4)
    m_DisplayConfig.epdResetPin = m_Properties.GetNumber("EPDResetPin", 0);
    m_DisplayConfig.epdBusyPin  = m_Properties.GetNumber("EPDBusyPin", 0);
    m_DisplayConfig.epdChipSelect = m_Properties.GetNumber("EPDChipSelect", 0);

    // Touch (für GT1151)
    m_DisplayConfig.touchResetPin = m_Properties.GetNumber("TouchResetPin", 0);
    m_DisplayConfig.touchIRQPin   = m_Properties.GetNumber("TouchIRQPin", 0);
    m_DisplayConfig.touchI2CAddress = m_Properties.GetNumber("TouchI2CAddress", 0x14);
}


// ───────────────────────────────────────────────
// Getter
// ───────────────────────────────────────────────

unsigned CConfig::GetPolyphony() const
{
    return m_nPolyphony;
}

const char* CConfig::GetSoundDevice() const
{
    return m_SoundDevice.c_str();
}

unsigned CConfig::GetSampleRate() const
{
    return m_nSampleRate;
}

unsigned CConfig::GetChunkSize() const
{
    return m_nChunkSize;
}

unsigned CConfig::GetDACI2CAddress() const
{
    return m_nDACI2CAddress;
}

bool CConfig::GetChannelsSwapped() const
{
    return m_bChannelsSwapped;
}

bool CConfig::GetTestToneEnabled() const
{
    return m_bTestToneEnabled;
}

unsigned CConfig::GetMIDIBaudRate() const
{
    return m_nMIDIBaudRate;
}

bool CConfig::GetMIDIThruEnabled() const
{
    return m_bMIDIThruEnabled;
}

unsigned CConfig::GetMIDIChannel() const
{
    return m_nMIDIChannel;
}

unsigned CConfig::GetMasterVolume() const
{
    return m_nMasterVolume;
}

DisplayConfig CConfig::GetDisplayConfig() const
{
    return m_DisplayConfig;
}

