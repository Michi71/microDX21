//
// kernel.cpp - Simplified multicore: Core 0 = Audio+MIDI, Core 2 = LVGL
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

// Quick LED blink (blocking) for early bootstrap diagnostics
static void BootBlink(unsigned count, unsigned onMs, unsigned offMs)
{
    for (unsigned i = 0; i < count; i++)
    {
        CActLED::Get()->On();
        CTimer::SimpleMsDelay(onMs);
        CActLED::Get()->Off();
        if (i + 1 < count)
            CTimer::SimpleMsDelay(offMs);
    }
}

CKernel* CKernel::s_pThis = nullptr;

CKernel::CKernel()
: CStdlibVKStdio("velvetkeys")
    , m_Config(&mFileSystem)
    , m_CPUThrottle(CPUSpeedMaximum)
    , m_GPIOManager(&mInterrupt)
    , m_I2CMaster (CMachineInfo::Get ()->GetDevice (DeviceI2CMaster), TRUE)
    , m_pSPIMaster(nullptr)
    , m_pVelvetKeys(nullptr)
    , m_pDisplayManager(nullptr)
    , m_pUSB(nullptr)
#if RASPPI == 4
    , m_pUSBHost(nullptr)
#endif
#if RASPPI < 5
    , m_pUSBGadget(nullptr)
#endif
    , m_Scheduler()
#ifdef ARM_ALLOW_MULTI_CORE
    , m_pMultiCore(nullptr)
#endif
    , m_bRunning(false)
{
    s_pThis = this;

#ifdef ARM_ALLOW_MULTI_CORE
    for (int i = 0; i < 4; i++)
        m_bCoresReady[i] = false;
#endif
}

CKernel::~CKernel()
{
    delete m_pDisplayManager;
    delete m_pVelvetKeys;
    delete m_pSPIMaster;
#if RASPPI < 5
    delete m_pUSBGadget;
#endif

#ifdef ARM_ALLOW_MULTI_CORE
    delete m_pMultiCore;
#endif
}

unsigned CKernel::GetNumberOfCores()
{
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

bool CKernel::Initialize()
{
    if (!CStdlibVKStdio::Initialize())
        return FALSE;

    CLogger::Get()->Write("kernel", LogNotice, "VelvetKeys starting...");

    if (!m_CPUThrottle.SetSpeed(CPUSpeedMaximum, true))
    {
        CLogger::Get()->Write("kernel", LogWarning, "Cannot set CPU speed");
    }

    m_Config.Load();

    // ───────────────────────────────────────────────
    // GPIO (wird für USB Gadget Pin VBUS-Detection benötigt)
    // ───────────────────────────────────────────────
    if (!m_GPIOManager.Initialize())
    {
        CLogger::Get()->Write("kernel", LogError, "GPIO init failed");
        return FALSE;
    }

    // ───────────────────────────────────────────────
    // USB: Host vs. Gadget Mode (je nach Config & Modell)
    //
    // RPi ≤ 3:   DWC2 ist der einzige USB-Controller → exklusives ODER
    // RPi 4:     DWC2 (USB-C) = Gadget, VL805/xHCI (USB-A) = Host
    //            → xHCI muss VOR dem Gadget initialisiert werden,
    //              damit der USB-Power-Domain aktiv ist (wie MiniDexed)
    // RPi 5:     Kein Gadget-Mode (per IsUSBGadget() blockiert)
    //
    // VBUS-Detection (wie MiniDexed):
    //   USBGadgetPin == 0  → keine Pin-Prüfung, immer Gadget-Mode
    //   USBGadgetPin != 0  → Pin lesen: LOW = Host connected → Gadget
    //                         HIGH = kein Host → Host-Mode (verhindert Hang)
    //   Erwartet: GPIO via Transistor/MOSFET auf GND gezogen,
    //   wenn VBUS (5V) vom USB-Host anliegt.
    // ───────────────────────────────────────────────
#if RASPPI < 5
    if (m_Config.IsUSBGadget())
    {
        CLogger::Get()->Write("kernel", LogNotice, "USB Gadget Mode requested");

        unsigned nGadgetPin = m_Config.GetUSBGadgetPin();
        bool bEnterGadget = true;   // ohne Pin-Prüfung: immer Gadget

        if (nGadgetPin != 0)
        {
            CGPIOPin vbusPin (nGadgetPin, GPIOModeInputPullUp);
            bEnterGadget = (vbusPin.Read () == LOW);
            CLogger::Get()->Write("kernel", LogNotice,
                                  "VBUS detect GPIO%u: %s",
                                  nGadgetPin,
                                  bEnterGadget ? "present → Gadget" : "absent → Host");
        }

        if (bEnterGadget)
        {
#if RASPPI == 4
            // RPi 4: xHCI (VL805) zuerst initialisieren, damit der USB-
            // Power-Domain aktiv ist, bevor der DWC2 als Gadget startet.
            if (mUSBHCI.Initialize())
            {
                m_pUSBHost = &mUSBHCI;
                CLogger::Get()->Write("kernel", LogNotice,
                                      "xHCI host (VL805) initialized");
            }
            else
            {
                CLogger::Get()->Write("kernel", LogWarning,
                                      "xHCI host init failed - USB-A ports unavailable");
            }
#endif

            m_pUSBGadget = new CUSBMIDIGadget(&mInterrupt);
            if (m_pUSBGadget->Initialize())
            {
                m_pUSB = m_pUSBGadget;
                CLogger::Get()->Write("kernel", LogNotice, "USB Gadget (MIDI) initialized");
            }
            else
            {
                CLogger::Get()->Write("kernel", LogError, "USB Gadget init failed");
                // KEIN delete — Circle-Destruktoren enthalten assert(0)
                m_pUSBGadget = nullptr;
            }
        }
        else
        {
            CLogger::Get()->Write("kernel", LogWarning,
                                  "Falling back to USB Host Mode");
            if (mUSBHCI.Initialize())
            {
                m_pUSB = &mUSBHCI;
            }
            else
            {
                CLogger::Get()->Write("kernel", LogError, "USB Host init failed");
            }
        }
    }
    else
#endif
    {
        CLogger::Get()->Write("kernel", LogNotice, "USB Host Mode");
        if (mUSBHCI.Initialize())
        {
            m_pUSB = &mUSBHCI;
        }
        else
        {
            CLogger::Get()->Write("kernel", LogError, "USB Host init failed");
        }
    }

    if (!m_I2CMaster.Initialize ())
    {
        CLogger::Get()->Write("kernel", LogError, "I2C Master init failed");
        return FALSE;
    }

    DisplayConfig dispCfg = m_Config.GetDisplayConfig();
    if (dispCfg.bus == DisplayConfig::Bus::SPI || dispCfg.bus == DisplayConfig::Bus::Auto)
    {
        unsigned nSPIBus = dispCfg.spiBus;
        unsigned nSPIMode = dispCfg.spiMode;
        unsigned long nSPIClock = dispCfg.spiSpeed;

        unsigned nCPHA = (nSPIMode & 1) ? 1 : 0;
        unsigned nCPOL = (nSPIMode & 2) ? 1 : 0;
        m_pSPIMaster = new CSPIMaster (nSPIClock, nCPOL, nCPHA, nSPIBus);

        if (!m_pSPIMaster->Initialize())
        {
            CLogger::Get()->Write("kernel", LogWarning, "SPI init failed");
            delete m_pSPIMaster;
            m_pSPIMaster = nullptr;
        }
    }

    // ───────────────────────────────────────────────
    // VELVETKEYS
    // ───────────────────────────────────────────────
    CLogger::Get()->Write("kernel", LogNotice, "Initializing VelvetKeys...");
    CLogger::Get()->Write("kernel", LogNotice,
                           "Audio config: %s %uHz ChunkSize=%u DACAddr=0x%02X",
                           m_Config.GetSoundDevice(),
                           m_Config.GetSampleRate(),
                           m_Config.GetChunkSize(),
                           m_Config.GetDACI2CAddress());
    const char* dev = m_Config.GetSoundDevice();

    if (strcmp(dev, "pwm") == 0)
    {
        m_pVelvetKeys = new CVelvetKeysPWM(&m_Config,
                                            &mInterrupt,
                                            &m_GPIOManager,
                                            &m_I2CMaster,
                                            &mFileSystem);
    }
    else if (strcmp(dev, "i2s") == 0)
    {
        //BootBlink(2, 200, 200);       // 2×: vor I2S-Konstruktor
        m_pVelvetKeys = new CVelvetKeysI2S(&m_Config,
                                            &mInterrupt,
                                            &m_GPIOManager,
                                            &m_I2CMaster,
                                            &mFileSystem);
        //BootBlink(3, 200, 200);       // 3×: Konstruktor OK
    }
#if RASPPI >= 4
    else if (strcmp(dev, "usb") == 0)
    {
        m_pVelvetKeys = new CVelvetKeysUSB(&m_Config,
                                            &mInterrupt,
                                            &m_GPIOManager,
                                            &m_I2CMaster,
                                            &mFileSystem);
    }
#endif

    assert(m_pVelvetKeys);
    //BootBlink(4, 200, 200);           // 4×: vor Initialize()
    if (!m_pVelvetKeys->Initialize())
    {
        CLogger::Get()->Write("kernel", LogError, "VelvetKeys init failed");
        BootBlink(4, 200, 200);
        return FALSE;
    }

    // ───────────────────────────────────────────────
    // DISPLAY MANAGER
    // ───────────────────────────────────────────────
    CLogger::Get()->Write("kernel", LogNotice, "Initializing Display...");

    m_pDisplayManager = new CDisplayManager(&m_I2CMaster,
                                        m_pSPIMaster,
                                         &m_GPIOManager,
                                        dispCfg,
                                         &mScreen);

    m_pDisplayManager->SetVelvetKeys(m_pVelvetKeys);

    if (!m_pDisplayManager->Initialize())
    {
        CLogger::Get()->Write("kernel", LogWarning, "Display init failed");
    }

#ifdef ARM_ALLOW_MULTI_CORE
    CLogger::Get()->Write("kernel", LogNotice, "Initializing Cores...");
    unsigned nCores = GetNumberOfCores();

    CLogger::Get()->Write("kernel", LogNotice,
                           "System has %u CPU cores", nCores);

    if (nCores >= 2)
    {
        m_pMultiCore = new CVelvetKeysMultiCore(this);

        if (!m_pMultiCore->Initialize())
        {
            CLogger::Get()->Write("kernel", LogError,
                                   "Multicore initialization failed!");
            delete m_pMultiCore;
            m_pMultiCore = nullptr;
        }
        else
        {
            CLogger::Get()->Write("kernel", LogNotice,
                                   "Multicore initialized!");

            m_bRunning.store(true, std::memory_order_release);
            DataMemBarrier ();
            m_bCoresReady[0] = true;
            DataSyncBarrier ();

            CTimer::Get()->MsDelay(100);

            CLogger::Get()->Write("kernel", LogNotice,
                                   "Secondary cores should now be running");
        }
    }
    else
    {
        CLogger::Get()->Write("kernel", LogNotice,
                               "Single-core mode (RPi 1)");
    }
#else
    CLogger::Get()->Write("kernel", LogNotice,
                           "Multicore support not compiled in");
#endif

    CLogger::Get()->Write("kernel", LogNotice, "Initialization complete!");

    return TRUE;
}

// ═══════════════════════════════════════════════════
// Core 0: MIDI + USB Plug & Play
// Audio (DMA interrupt) läuft auf Core 0, holt aber Daten
// aus dem Double-Buffer (produziert von Core 1).
// ═══════════════════════════════════════════════════
CStdlibVKStdio::TShutdownMode CKernel::Run()
{
    assert(m_pVelvetKeys);

#ifdef ARM_ALLOW_MULTI_CORE
    CLogger::Get()->Write("kernel", LogNotice,
                           "Core 0: MIDI + USB Plug & Play");
#else
    CLogger::Get()->Write("kernel", LogNotice,
                           "Core 0: Audio + MIDI + Display + Deferred Work (Single-Core)");
#endif

    while (m_bRunning.load(std::memory_order_acquire))
    {
        DataMemBarrier ();
        bool bUpdated = false;

        if (m_pUSB)
            bUpdated = m_pUSB->UpdatePlugAndPlay();

// xHCI-Host-Polling (RPi 4): parallel zum Gadget (USB-A Ports)
#if RASPPI == 4
        if (m_pUSBHost)
            bUpdated |= m_pUSBHost->UpdatePlugAndPlay();
#endif

        // MIDI processing
        m_pVelvetKeys->Process(bUpdated);

#ifndef ARM_ALLOW_MULTI_CORE
        // Single-Core: Display und Deferred Work laufen auf Core 0
        if (m_pDisplayManager)
        {
            m_pDisplayManager->ProcessEncoderEvents();
            m_pDisplayManager->ProcessDisplayThread();
        }
#endif

        m_CPUThrottle.Update();

        CScheduler::Get()->Yield();
    }

    CLogger::Get()->Write("kernel", LogNotice, "Core 0: Stopped");
    return ShutdownHalt;
}

#ifdef ARM_ALLOW_MULTI_CORE
// ═══════════════════════════════════════════════════
// Core 1: Audio Generation (Synth Engine)
// Produziert kontinuierlich Audio-Blöcke in den Double-Buffer.
// Core 0/DMA-IRQ holt die fertigen Blöcke ab.
// ═══════════════════════════════════════════════════
void CKernel::RunCore1()
{
    CLogger::Get()->Write("kernel", LogNotice,
                           "Core 1: Audio Generation");

    DataMemBarrier ();
    m_bCoresReady[1] = true;
    DataSyncBarrier ();

    while (m_bRunning.load(std::memory_order_acquire))
    {
        if (m_pVelvetKeys)
        {
            // FillAudioSlot() liefert false, wenn alle Buffer-Slots
            // bereits Ready sind. Dann kurz warten, statt wie bisher
            // in einem Hot-Spin durch den Drop-One-Pfad Ready-Slots
            // zu überschreiben (das verursachte Phasensprünge zwischen
            // den Slots → Stottern/Knacken in der I2S-Wiedergabe).
            //
            // Wir warten ein Viertel-Chunk:
            //   ChunkSize / 2 Frames @ SampleRate
            //   → 1/4 davon = (ChunkSize / 8) / SampleRate Sekunden
            // Bei 1024 / 48000 Hz ≈ 2.7 ms — eine Größenordnung kürzer
            // als die DMA-Periode (10.7 ms), reagiert also schnell genug.
            if (!m_pVelvetKeys->FillAudioSlot())
            {
                unsigned us = (m_pVelvetKeys->GetChunkSize() * 1000000u)
                              / (m_pVelvetKeys->GetSampleRate() * 8u);
                if (us < 100)  us = 100;       // Untergrenze
                if (us > 5000) us = 5000;      // Obergrenze
                CTimer::Get()->usDelay(us);
            }
        }
    }

    CLogger::Get()->Write("kernel", LogNotice, "Core 1: Stopped");
}

// ═══════════════════════════════════════════════════
// Core 2: Display Manager (LVGL + Encoder)
// Isoliert von Audio. Pollt Encoder-Events und aktualisiert LVGL.
// ═══════════════════════════════════════════════════
void CKernel::RunCore2()
{
    CLogger::Get()->Write("kernel", LogNotice,
                           "Core 2: Display Manager (LVGL + Encoder)");

    DataMemBarrier ();
    m_bCoresReady[2] = true;
    DataSyncBarrier ();

    while (m_bRunning.load(std::memory_order_acquire))
    {
        if (m_pDisplayManager)
        {
            m_pDisplayManager->ProcessEncoderEvents();
            m_pDisplayManager->ProcessDisplayThread();
        }
        CTimer::Get()->usDelay(1000); // ~1 kHz LVGL update rate
    }

    CLogger::Get()->Write("kernel", LogNotice, "Core 2: Stopped");
}

// ═══════════════════════════════════════════════════
// Core 3: Deferred Work (SysEx, Preset-Load, File I/O)
// Schwere Operationen, die Audio pausieren können.
// ═══════════════════════════════════════════════════
void CKernel::RunCore3()
{
    CLogger::Get()->Write("kernel", LogNotice,
                           "Core 3: Deferred Work (SysEx, File I/O)");

    DataMemBarrier ();
    m_bCoresReady[3] = true;
    DataSyncBarrier ();

    while (m_bRunning.load(std::memory_order_acquire))
    {
        if (m_pVelvetKeys)
            m_pVelvetKeys->ProcessDeferredWork();

        CTimer::Get()->usDelay(100); // 10 kHz Poll-Rate für Deferred Work
    }

    CLogger::Get()->Write("kernel", LogNotice, "Core 3: Stopped");
}
#endif

void CKernel::PanicHandler()
{
    EnableIRQs();
    if (s_pThis)
    {
        s_pThis->m_bRunning.store(false, std::memory_order_release);
    }
    DataSyncBarrier();
    while (true)
    {
        __asm__ volatile("wfi");
    }
}