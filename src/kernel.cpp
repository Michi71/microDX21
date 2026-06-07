//
// kernel.cpp - microDX21 bare-metal kernel for Raspberry Pi.
//
// Cores (Pi 3/4/5 multicore):
//   Core 0: USB PnP poll, MIDI, audio double-buffer consumer (DMA IRQ)
//   Core 1: Audio generation (FillAudioSlot -> lockfree ringbuffer)
//   Core 2: Display update + rotary encoder state machine
//   Core 3: Deferred work (SysEx, preset load, file I/O)
//
// Single-core (Pi 1/2/Zero without ARM_ALLOW_MULTI_CORE):
//   Core 0: everything (USB + MIDI + audio + display + deferred)
//
// Display: 128x32 SSD1305 SPI OLED, driven by CDX21Display, which uses
// libdisplay2's CSSD1305SPIDisplay for raw pixel access.
// Input: a single KY-040 rotary encoder via CKY040 (Circle sensor addon).
//

#include "kernel.h"
#include <circle/string.h>
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/bcm2835.h>
#include <circle/gpiopin.h>
#include <circle/actled.h>
#include <circle/timer.h>
#include <assert.h>

// Quick LED blink (blocking) for early bootstrap diagnostics.
static void BootBlink(unsigned count, unsigned onMs, unsigned offMs) {
    for (unsigned i = 0; i < count; i++) {
        CActLED::Get()->On();
        CTimer::SimpleMsDelay(onMs);
        CActLED::Get()->Off();
        if (i + 1 < count) CTimer::SimpleMsDelay(offMs);
    }
}

CKernel* CKernel::s_pThis = nullptr;

CKernel::CKernel()
: CStdlibVKStdio("microdx21")
, m_Config(&mFileSystem)
, m_CPUThrottle(CPUSpeedMaximum)
, m_GPIOManager(&mInterrupt)
, m_I2CMaster(CMachineInfo::Get()->GetDevice(DeviceI2CMaster), TRUE)
, m_pSPIMaster(nullptr)
, m_pMicroDX21(nullptr)
, m_pUSB(nullptr)
#if RASPPI == 4
, m_pUSBHost(nullptr)
#endif
#if RASPPI < 5
, m_pUSBGadget(nullptr)
#endif
, m_Scheduler()
, m_pDX21Display(nullptr)
, m_pDX21Input(nullptr)
#ifdef ARM_ALLOW_MULTI_CORE
, m_pMultiCore(nullptr)
, m_bCoresReady()
#endif
, m_bRunning(false)
{
    s_pThis = this;
#ifdef ARM_ALLOW_MULTI_CORE
    for (int i = 0; i < 4; i++) m_bCoresReady[i] = false;
#endif
}

CKernel::~CKernel() {
    delete m_pDX21Input;
    delete m_pDX21Display;
    delete m_pMicroDX21;
    delete m_pSPIMaster;
#if RASPPI < 5
    delete m_pUSBGadget;
#endif
#ifdef ARM_ALLOW_MULTI_CORE
    delete m_pMultiCore;
#endif
}

unsigned CKernel::GetNumberOfCores() {
#if RASPPI == 1
    return 1;
#elif RASPPI == 2
    return 4;
#elif RASPPI == 3
    return 4;
#elif RASPPI == 4
    return 4;
#elif RASPPI == 5
    return 4;
#else
    return 1;
#endif
}

bool CKernel::Initialize() {
    if (!CStdlibVKStdio::Initialize())
        return FALSE;

    CLogger::Get()->Write("kernel", LogNotice, "microDX21 starting...");

    if (!m_CPUThrottle.SetSpeed(CPUSpeedMaximum, true)) {
        CLogger::Get()->Write("kernel", LogWarning, "Cannot set CPU speed");
    }

    m_Config.Load();

    // ───────────────────────────────────────────────
    // GPIO (needed for USB Gadget VBUS detection + encoder ISR)
    // ───────────────────────────────────────────────
    if (!m_GPIOManager.Initialize()) {
        CLogger::Get()->Write("kernel", LogError, "GPIO init failed");
        return FALSE;
    }

    // ───────────────────────────────────────────────
    // USB: Host vs. Gadget Mode
    //   RPi <= 3 : DWC2, exclusive OR
    //   RPi 4    : DWC2 (USB-C) = Gadget, VL805/xHCI (USB-A) = Host
    //              xHCI must init BEFORE Gadget for the USB power domain
    //   RPi 5    : No Gadget mode
    // ───────────────────────────────────────────────
#if RASPPI < 5
    if (m_Config.IsUSBGadget()) {
        CLogger::Get()->Write("kernel", LogNotice, "USB Gadget Mode requested");
        unsigned nGadgetPin = m_Config.GetUSBGadgetPin();
        bool bEnterGadget = true;
        if (nGadgetPin != 0) {
            CGPIOPin vbusPin(nGadgetPin, GPIOModeInputPullUp);
            bEnterGadget = (vbusPin.Read() == LOW);
            CLogger::Get()->Write("kernel", LogNotice,
                "VBUS detect GPIO%u: %s", nGadgetPin,
                bEnterGadget ? "present -> Gadget" : "absent -> Host");
        }
        if (bEnterGadget) {
#if RASPPI == 4
            if (mUSBHCI.Initialize()) {
                m_pUSBHost = &mUSBHCI;
                CLogger::Get()->Write("kernel", LogNotice, "xHCI host up (RPi4)");
            } else {
                CLogger::Get()->Write("kernel", LogWarning, "xHCI init failed");
            }
#endif
            m_pUSBGadget = new CUSBMIDIGadget(&mInterrupt);
            if (m_pUSBGadget->Initialize()) {
                m_pUSB = m_pUSBGadget;
            } else {
                CLogger::Get()->Write("kernel", LogWarning, "Gadget init failed");
                delete m_pUSBGadget;
                m_pUSBGadget = nullptr;
            }
        }
    }
    if (!m_pUSB) {
        CLogger::Get()->Write("kernel", LogNotice, "USB Host Mode");
        if (mUSBHCI.Initialize()) {
            m_pUSB = &mUSBHCI;
        } else {
            CLogger::Get()->Write("kernel", LogError, "USB Host init failed");
        }
    }
#else
    CLogger::Get()->Write("kernel", LogNotice, "USB Host Mode (RPi5)");
    if (mUSBHCI.Initialize()) {
        m_pUSB = &mUSBHCI;
    } else {
        CLogger::Get()->Write("kernel", LogError, "USB Host init failed");
    }
#endif

    if (!m_I2CMaster.Initialize()) {
        CLogger::Get()->Write("kernel", LogError, "I2C Master init failed");
        return FALSE;
    }

    // ───────────────────────────────────────────────
    // SPI (for SSD1305 OLED)
    // ───────────────────────────────────────────────
    DisplayConfig dispCfg = m_Config.GetDisplayConfig();
    if (dispCfg.bus == DisplayConfig::Bus::SPI
        || dispCfg.bus == DisplayConfig::Bus::Auto) {
        unsigned nCPHA = (dispCfg.spiMode & 1) ? 1 : 0;
        unsigned nCPOL = (dispCfg.spiMode & 2) ? 1 : 0;
        m_pSPIMaster = new CSPIMaster(dispCfg.spiSpeed, nCPOL, nCPHA,
                                      dispCfg.spiBus);
        if (!m_pSPIMaster->Initialize()) {
            CLogger::Get()->Write("kernel", LogWarning, "SPI init failed");
            delete m_pSPIMaster;
            m_pSPIMaster = nullptr;
        }
    }

    // ───────────────────────────────────────────────
    // Audio engine
    // ───────────────────────────────────────────────
    CLogger::Get()->Write("kernel", LogNotice, "Initializing MicroDX21...");
    CLogger::Get()->Write("kernel", LogNotice,
        "Audio: %s %uHz ChunkSize=%u DACAddr=0x%02X",
        m_Config.GetSoundDevice(), m_Config.GetSampleRate(),
        m_Config.GetChunkSize(), m_Config.GetDACI2CAddress());
    const char* dev = m_Config.GetSoundDevice();
    if (strcmp(dev, "pwm") == 0) {
        m_pMicroDX21 = new CMicroDX21PWM(&m_Config, &mInterrupt, &m_GPIOManager,
                                         &m_I2CMaster, &mFileSystem);
    } else if (strcmp(dev, "i2s") == 0) {
        m_pMicroDX21 = new CMicroDX21I2S(&m_Config, &mInterrupt, &m_GPIOManager,
                                         &m_I2CMaster, &mFileSystem);
    }
#if RASPPI >= 4
    else if (strcmp(dev, "usb") == 0) {
        m_pMicroDX21 = new CMicroDX21USB(&m_Config, &mInterrupt, &m_GPIOManager,
                                         &m_I2CMaster, &mFileSystem);
    }
#endif
    assert(m_pMicroDX21);
    if (!m_pMicroDX21->Initialize()) {
        CLogger::Get()->Write("kernel", LogError, "MicroDX21 init failed");
        BootBlink(4, 200, 200);
        return FALSE;
    }

    // ───────────────────────────────────────────────
    // Display (128x32 SSD1305 SPI)
    // ───────────────────────────────────────────────
    if (m_pSPIMaster && dispCfg.controller == DisplayConfig::Controller::SSD1305) {
        CDX21Display::Config dcfg;
        dcfg.nDCPin      = dispCfg.pinDC;
        dcfg.nResetPin   = dispCfg.pinReset;
        dcfg.nChipSelect = dispCfg.spiBus == 0 ? 0 : 1;  // CE0 vs CE1
        dcfg.nClockSpeed = dispCfg.spiSpeed;
        dcfg.nCPOL = (dispCfg.spiMode & 2) ? 1 : 0;
        dcfg.nCPHA = (dispCfg.spiMode & 1) ? 1 : 0;

        m_pDX21Display = new CDX21Display(m_pSPIMaster, dcfg);
        if (m_pDX21Display && m_pDX21Display->Initialize()) {
            m_pDX21Display->SetMode(DX21UI::kModePlay);
            m_pDX21Display->SetVoiceName("Init Voice");
            m_pDX21Display->SetVoiceNumber(1);
            m_pDX21Display->SetParamIndex(0);
            m_pDX21Display->SetValue(50);
            m_pDX21Display->SetStatus("microDX21 ready");

            // Bind the synth adapter so Render() can read live
            // parameter values (voice name, play mode, EDIT-mode
            // 0..255 byte values, etc.) directly from the synth.
            m_pDX21Display->SetAdapter(m_pMicroDX21->GetOPMEmuAdapter());
            m_pDX21Display->Render();
        } else {
            CLogger::Get()->Write("kernel", LogWarning, "Display init failed");
            delete m_pDX21Display;
            m_pDX21Display = nullptr;
        }
    } else {
        CLogger::Get()->Write("kernel", LogNotice,
            "No display (SPI=%p ctrl=%d)", m_pSPIMaster,
            (int)dispCfg.controller);
    }

    // ───────────────────────────────────────────────
    // Encoder (KY-040)
    // ───────────────────────────────────────────────
    if (m_pDX21Display) {
        CDX21Input::Config ecfg;
        ecfg.nPinClk  = dispCfg.encPinA;
        ecfg.nPinDt   = dispCfg.encPinB;
        ecfg.nPinSw   = dispCfg.encPinBtn;
        m_pDX21Input = new CDX21Input(m_pDX21Display, &m_GPIOManager, ecfg);
        if (!m_pDX21Input->Initialize()) {
            CLogger::Get()->Write("kernel", LogWarning, "Encoder init failed");
            delete m_pDX21Input;
            m_pDX21Input = nullptr;
        }
    }

    // ───────────────────────────────────────────────
    // Multi-core launch
    // ───────────────────────────────────────────────
#ifdef ARM_ALLOW_MULTI_CORE
    CLogger::Get()->Write("kernel", LogNotice, "Initializing cores...");
    unsigned nCores = GetNumberOfCores();
    CLogger::Get()->Write("kernel", LogNotice, "System has %u cores", nCores);
    if (nCores >= 2) {
        m_pMultiCore = new CMicroDX21MultiCore(this);
        if (!m_pMultiCore->Initialize()) {
            CLogger::Get()->Write("kernel", LogError, "MultiCore init failed");
            return FALSE;
        }
    }
#endif

    m_bRunning.store(true, std::memory_order_release);
    CLogger::Get()->Write("kernel", LogNotice, "Initialize OK");
    return TRUE;
}

// ═══════════════════════════════════════════════════
// Core 0: USB PnP poll + MIDI
// ═══════════════════════════════════════════════════
CStdlibVKStdio::TShutdownMode CKernel::Run() {
    assert(m_pMicroDX21);

#ifdef ARM_ALLOW_MULTI_CORE
    CLogger::Get()->Write("kernel", LogNotice, "Core 0: MIDI + USB PnP");
#else
    CLogger::Get()->Write("kernel", LogNotice,
        "Core 0: Audio + MIDI + Display + Deferred (Single-Core)");
#endif

    while (m_bRunning.load(std::memory_order_acquire)) {
        DataMemBarrier();
        bool bUpdated = false;
        if (m_pUSB) bUpdated = m_pUSB->UpdatePlugAndPlay();
#if RASPPI == 4
        if (m_pUSBHost) bUpdated |= m_pUSBHost->UpdatePlugAndPlay();
#endif
        m_pMicroDX21->Process(bUpdated);

        if (m_pDX21Display) m_pDX21Display->Render();
        m_CPUThrottle.Update();
        CScheduler::Get()->Yield();
    }
    CLogger::Get()->Write("kernel", LogNotice, "Core 0: Stopped");
    return ShutdownHalt;
}

#ifdef ARM_ALLOW_MULTI_CORE
// ═══════════════════════════════════════════════════
// Core 1: Audio Generation
// Fills the lockfree ringbuffer with new chunks. DMA-IRQ on Core 0
// consumes them.
// ═══════════════════════════════════════════════════
void CKernel::RunCore1() {
    CLogger::Get()->Write("kernel", LogNotice, "Core 1: Audio Generation");
    DataMemBarrier();
    m_bCoresReady[1] = true;
    DataSyncBarrier();
    while (m_bRunning.load(std::memory_order_acquire)) {
        if (m_pMicroDX21) {
            if (!m_pMicroDX21->FillAudioSlot()) {
                unsigned us = (m_pMicroDX21->GetChunkSize() * 1000000u)
                              / (m_pMicroDX21->GetSampleRate() * 8u);
                if (us < 100)  us = 100;
                if (us > 5000) us = 5000;
                CTimer::Get()->usDelay(us);
            }
        }
    }
    CLogger::Get()->Write("kernel", LogNotice, "Core 1: Stopped");
}

// ═══════════════════════════════════════════════════
// Core 2: Display + Encoder
// Renders the 128x32 OLED and polls the encoder. The encoder's event
// handler (registered in CDX21Input::Initialize) runs in ISR context on
// GPIO edges; it only queues state changes that the next Render() picks
// up. This loop drives Render() at ~30 Hz which is plenty for a small
// OLED and lets the encoder events drain in the same thread.
// ═══════════════════════════════════════════════════
void CKernel::RunCore2() {
    CLogger::Get()->Write("kernel", LogNotice, "Core 2: Display + Encoder");
    DataMemBarrier();
    m_bCoresReady[2] = true;
    DataSyncBarrier();

    // Initial paint
    if (m_pDX21Display) m_pDX21Display->Render();

    while (m_bRunning.load(std::memory_order_acquire)) {
        if (m_pDX21Display) m_pDX21Display->Render();
        CTimer::Get()->usDelay(33000);  // ~30 Hz
    }
    CLogger::Get()->Write("kernel", LogNotice, "Core 2: Stopped");
}

// ═══════════════════════════════════════════════════
// Core 3: Deferred Work (SysEx, preset load, file I/O)
// ═══════════════════════════════════════════════════
void CKernel::RunCore3() {
    CLogger::Get()->Write("kernel", LogNotice, "Core 3: Deferred Work");
    DataMemBarrier();
    m_bCoresReady[3] = true;
    DataSyncBarrier();
    while (m_bRunning.load(std::memory_order_acquire)) {
        if (m_pMicroDX21) m_pMicroDX21->ProcessDeferredWork();
        CTimer::Get()->usDelay(100);
    }
    CLogger::Get()->Write("kernel", LogNotice, "Core 3: Stopped");
}
#endif

void CKernel::PanicHandler() {
    EnableIRQs();
    if (s_pThis) {
        s_pThis->m_bRunning.store(false, std::memory_order_release);
    }
    DataSyncBarrier();
    while (true) {
        __asm__ volatile("wfi");
    }
}
