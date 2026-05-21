//
// kernel.h
//
// VelvetKeys - A bare metal synthesizer for Raspberry Pi
// Based on COPMEmu FM synthesizer engine (DX21)

//
#ifndef _kernel_h
#define _kernel_h

#include "circle_stdlib_app.h"
#include <circle/cputhrottle.h>
#include <circle/gpiomanager.h>
#include <circle/i2cmaster.h>
#include <circle/spimaster.h>
#include <circle/usb/usbcontroller.h>
#if RASPPI == 4
#include <circle/usb/usbhostcontroller.h>
#endif
#if RASPPI < 5
#include <circle/usb/gadget/usbmidigadget.h>
#endif
#include <circle/sched/scheduler.h>
#include <atomic>
#include "config.h"
#include "velvetkeys.h"
#include "velvetkeys_pwm.h"
#include "velvetkeys_i2s.h"
#include "velvetkeys_usb.h"
#include "displaymanager.h"
#include "circle_stdlib_vk.h"

#ifdef ARM_ALLOW_MULTI_CORE
#include "velvetkeys_multicore.h"
#endif

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel : public CStdlibVKStdio
{
public:
    CKernel();
     ~CKernel();

    bool Initialize();
    TShutdownMode Run();

#ifdef ARM_ALLOW_MULTI_CORE
     // WICHTIG: Public machen, damit CVelvetKeysMultiCore darauf zugreifen kann
    void RunCore1();    // Audio Prebuffer
    void RunCore2();    // Display
    void RunCore3();    // Audio Prep (Fallback)
#endif

private:
    static void PanicHandler();

     // Helper
    static unsigned GetNumberOfCores();

#ifdef ARM_ALLOW_MULTI_CORE
     // Multicore entry points
    static void SecondaryCore2Entry();
    static void SecondaryCore3Entry();
#endif

private:
    CConfig         m_Config;
    CCPUThrottle    m_CPUThrottle;
    CGPIOManager    m_GPIOManager;
    CI2CMaster      m_I2CMaster;
    CSPIMaster*     m_pSPIMaster;
    CVelvetKeys*    m_pVelvetKeys;
    CDisplayManager* m_pDisplayManager;
    CUSBController* m_pUSB;
#if RASPPI == 4
    CUSBHostController* m_pUSBHost;     // xHCI (VL805) für USB-A Ports im Gadget-Mode
#endif
#if RASPPI < 5
    CUSBMIDIGadget* m_pUSBGadget;
#endif
    CScheduler      m_Scheduler;

#ifdef ARM_ALLOW_MULTI_CORE
    CVelvetKeysMultiCore* m_pMultiCore;    // NEU
    volatile bool   m_bCoresReady[4];
#endif
    std::atomic<bool> m_bRunning;            // Für Single-Core & Multicore

    static CKernel* s_pThis;
};

#endif
