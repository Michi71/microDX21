//
// kernel.h
//
// microDX21 - a Yamaha DX21 emulator for Raspberry Pi (bare-metal Circle).
// Single-encoder + small SSD1305/SPI OLED (128x32) UI. No LVGL.
//

#ifndef _kernel_h
#define _kernel_h

#include <circle_stdlib_app.h>
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
#include "microdx21.h"
#include "audio/microdx21_pwm.h"
#include "audio/microdx21_i2s.h"
#include "audio/microdx21_usb.h"
#include "circle_stdlib_vk.h"
#include "display/display_dx21.h"
#include "display/dx21_input.h"

#ifdef ARM_ALLOW_MULTI_CORE
#include "audio/microdx21_multicore.h"
#endif

enum TShutdownMode {
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel : public CStdlibVKStdio {
public:
    CKernel();
    ~CKernel();

    bool Initialize();
    TShutdownMode Run();

#ifdef ARM_ALLOW_MULTI_CORE
    void RunCore1();  // Audio generation
    void RunCore2();  // Display + encoder
    void RunCore3();  // Deferred work
#endif

private:
    static void PanicHandler();
    static unsigned GetNumberOfCores();

#ifdef ARM_ALLOW_MULTI_CORE
    static void SecondaryCore2Entry();
    static void SecondaryCore3Entry();
#endif

private:
    CConfig         m_Config;
    CCPUThrottle    m_CPUThrottle;
    CGPIOManager    m_GPIOManager;
    CI2CMaster      m_I2CMaster;
    CSPIMaster*     m_pSPIMaster;
    CMicroDX21*     m_pMicroDX21;
    CUSBController* m_pUSB;
#if RASPPI == 4
    CUSBHostController* m_pUSBHost;
#endif
#if RASPPI < 5
    CUSBMIDIGadget* m_pUSBGadget;
#endif
    CScheduler      m_Scheduler;

    // Display + input. Set in Initialize(), torn down in destructor.
    CDX21Display*   m_pDX21Display;
    CDX21Input*     m_pDX21Input;

#ifdef ARM_ALLOW_MULTI_CORE
    CMicroDX21MultiCore* m_pMultiCore;
    volatile bool   m_bCoresReady[4];
#endif
    std::atomic<bool> m_bRunning;

    static CKernel* s_pThis;
};

#endif
