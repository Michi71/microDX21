//
// config.cpp – microDX21 application configuration
//
// Reads /boot/microdx21.ini on the SD card (via FatFS). All keys are
// optional; missing keys fall back to sensible defaults that work for
// the most common Pi 3/4/5 + I2S DAC + SSD1305 SPI OLED setup.
//
// A complete example lives in config/microdx21.ini.
//

#include "config.h"

CConfig::CConfig(FATFS* pFileSystem)
: m_Properties("microdx21.ini", pFileSystem)

{
}

CConfig::~CConfig()
{
}

void CConfig::Load()
{
    m_Properties.Load();

    auto trim = [](std::string &s)
    {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
            s.pop_back();
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.erase(0, 1);
    };

    // ───────────────────────────────────────────────
    // Audio
    // ───────────────────────────────────────────────
    m_SoundDevice      = m_Properties.GetString("SoundDevice", "i2s");
    trim(m_SoundDevice);

    m_nSampleRate      = m_Properties.GetNumber("SampleRate", 48000);
    m_nChunkSize       = m_Properties.GetNumber("ChunkSize", 256);
    m_nDACI2CAddress   = m_Properties.GetNumber("DACI2CAddress", 0);
    m_bChannelsSwapped = m_Properties.GetNumber("ChannelsSwapped", 0) != 0;
    m_bTestToneEnabled = m_Properties.GetNumber("TestTone", 0) != 0;

    // ───────────────────────────────────────────────
    // MIDI
    // ───────────────────────────────────────────────
    m_nMIDIBaudRate    = m_Properties.GetNumber("MIDIBaudRate", 31250);
    m_bMIDIThruEnabled = m_Properties.GetNumber("MIDIThruEnabled", 1) != 0;

    // 0 = Omni (accept all channels), 1..16 = specific channel.
    // Out-of-range values fall back to 0 so a typo never hard-mutes the synth.
    m_nMIDIChannel = m_Properties.GetNumber("MIDIChannel", 0);
    if (m_nMIDIChannel > 16)
        m_nMIDIChannel = 0;

    // VelocityCurve: 0=Linear 1=Soft 2=Hard 3=DX21 4=Softest.
    // Also accept named strings: linear, soft, hard, dx21, softest.
    std::string velCurve = m_Properties.GetString("VelocityCurve", "linear");
    trim(velCurve);
    for (auto& c : velCurve) c = static_cast<char>(::tolower(c));
    if      (velCurve == "linear")  m_nVelocityCurve = 0;
    else if (velCurve == "soft")    m_nVelocityCurve = 1;
    else if (velCurve == "hard")    m_nVelocityCurve = 2;
    else if (velCurve == "dx21")    m_nVelocityCurve = 3;
    else if (velCurve == "softest") m_nVelocityCurve = 4;
    else {
        // Try parsing as integer for backward compat
        char* endp = nullptr;
        long n = ::strtol(velCurve.c_str(), &endp, 10);
        if (endp != velCurve.c_str() && n >= 0 && n <= 4) {
            m_nVelocityCurve = static_cast<unsigned>(n);
        } else {
            m_nVelocityCurve = 0;  // unknown → Linear
        }
    }

    // ───────────────────────────────────────────────
    // USB
    // ───────────────────────────────────────────────

    // ───────────────────────────────────────────────
    // Master Volume
    // ───────────────────────────────────────────────
    m_nMasterVolume = m_Properties.GetNumber("MasterVolume", 100);

    // ───────────────────────────────────────────────
    // Polyphony
    //
    // MaxNotes is the absolute hardware/algorithm cap. Any INI value
    // above MaxNotes is clamped down to MaxNotes — we don't fall back
    // to the default, because a user who wrote "Polyphony=128" most
    // likely wants "as many voices as possible", not the conservative
    // default. Sub-1 values are nonsensical; clamp to 1.
    // ───────────────────────────────────────────────
    m_nPolyphony = m_Properties.GetNumber("Polyphony", DefaultNotes);
    if (m_nPolyphony > MaxNotes)
        m_nPolyphony = MaxNotes;
    if (m_nPolyphony < 1)
        m_nPolyphony = 1;

    // ───────────────────────────────────────────────
    // DISPLAY
    // ───────────────────────────────────────────────
    m_DisplayConfig.enabled =
        m_Properties.GetNumber("DisplayEnabled", 1) != 0;

    // Controller
    std::string ctrl = m_Properties.GetString("DisplayType", "ssd1305");
    trim(ctrl);

    if (ctrl == "ssd1306")         m_DisplayConfig.controller = DisplayConfig::Controller::SSD1306;
    else if (ctrl == "ssd1305")    m_DisplayConfig.controller = DisplayConfig::Controller::SSD1305;
    else if (ctrl == "sh1106")     m_DisplayConfig.controller = DisplayConfig::Controller::SH1106;
    else /* default: ssd1305 */    m_DisplayConfig.controller = DisplayConfig::Controller::SSD1305;

    // Bus
    std::string bus = m_Properties.GetString("DisplayBus", "spi");
    trim(bus);

    if (bus == "i2c")              m_DisplayConfig.bus = DisplayConfig::Bus::I2C;
    else /* default: spi */        m_DisplayConfig.bus = DisplayConfig::Bus::SPI;

    // Resolution
    m_DisplayConfig.resolution.width  = m_Properties.GetNumber("DisplayWidth", 128);
    m_DisplayConfig.resolution.height = m_Properties.GetNumber("DisplayHeight", 32);

    // I2C
    m_DisplayConfig.i2cAddress = m_Properties.GetNumber("DisplayI2CAddress", 0x3C);

    // SPI — used by kernel.cpp to init both the bus master and the OLED.
    // SPI CS is hardcoded: CE0 for spiBus=0, CE1 otherwise.
    m_DisplayConfig.spiBus   = m_Properties.GetNumber("DisplaySPIBus", 0);
    m_DisplayConfig.spiMode  = m_Properties.GetNumber("DisplaySPIMode", 0);
    m_DisplayConfig.pinDC    = m_Properties.GetNumber("DisplaySPIDCPin", 24);
    m_DisplayConfig.pinReset = m_Properties.GetNumber("DisplaySPIResetPin", 25);
    m_DisplayConfig.spiSpeed = m_Properties.GetNumber("DisplaySPISpeed", 8000000);

    // Encoder (KY-040, BCM pin numbers)
    m_DisplayConfig.encPinA   = m_Properties.GetNumber("EncoderPinA", 10);
    m_DisplayConfig.encPinB   = m_Properties.GetNumber("EncoderPinB", 9);
    m_DisplayConfig.encPinBtn = m_Properties.GetNumber("EncoderPinBtn", 11);
}


// ───────────────────────────────────────────────
// Getter
// ───────────────────────────────────────────────────────────────────────

unsigned CConfig::GetPolyphony() const { return m_nPolyphony; }

const char* CConfig::GetSoundDevice() const { return m_SoundDevice.c_str(); }

unsigned CConfig::GetSampleRate() const { return m_nSampleRate; }

unsigned CConfig::GetChunkSize() const { return m_nChunkSize; }

unsigned CConfig::GetDACI2CAddress() const { return m_nDACI2CAddress; }

bool CConfig::GetChannelsSwapped() const { return m_bChannelsSwapped; }

bool CConfig::GetTestToneEnabled() const { return m_bTestToneEnabled; }

unsigned CConfig::GetMIDIBaudRate() const { return m_nMIDIBaudRate; }

bool CConfig::GetMIDIThruEnabled() const { return m_bMIDIThruEnabled; }

unsigned CConfig::GetMIDIChannel() const { return m_nMIDIChannel; }
unsigned CConfig::GetVelocityCurve() const { return m_nVelocityCurve; }

unsigned CConfig::GetMasterVolume() const { return m_nMasterVolume; }

DisplayConfig CConfig::GetDisplayConfig() const { return m_DisplayConfig; }
