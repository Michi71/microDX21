/**
 * Convenience classes that package different levels
 * of functionality of Circle applications.
 *
 * Derive the kernel class of the application from one of
 * the CStdlibVK* classes and implement at least the
 * Run () method. Extend the Initalize () and Cleanup ()
 * methods if necessary.
 */
#ifndef _circle_stdlib_vk_h
#define _circle_stdlib_vk_h

#include <circle/actled.h>
#include <circle/string.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/screen.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <SDCard/emmc.h>
#include <circle/input/console.h>
#include <circle/sched/scheduler.h>
#include <circle/net/netsubsystem.h>
#include <wlan/bcm4343.h>
#include <wlan/hostap/wpa_supplicant/wpasupplicant.h>

#include <circle_glue.h>
#include <cassert>
#include <wrap_fatfs.h>
#include <cstring>
#include <stdexcept>

/**
 * Basic Circle Stdlib application that supports GPIO access.
 */
class CStdlibVK
{
public:
        enum TShutdownMode
        {
                ShutdownNone,
                ShutdownHalt,
                ShutdownReboot
        };

        CStdlibVK (const char *kernel) :
                FromKernel (kernel)
        {
                assert (FromKernel != nullptr);
        }

        virtual ~CStdlibVK (void)
        {
        }

        virtual bool Initialize (void)
        {
                return mInterrupt.Initialize ();
        }

        virtual void Cleanup (void)
        {
        }

        virtual TShutdownMode Run (void) = 0;

        const char *GetKernelName(void) const
        {
                return FromKernel;
        }

protected:
        CActLED            mActLED;
        CKernelOptions     mOptions;
        CDeviceNameService mDeviceNameService;
        CNullDevice        mNullDevice;
        CExceptionHandler  mExceptionHandler;
        CInterruptSystem   mInterrupt;

private:
        char const *FromKernel;
};

/**
 * Stdlib application that adds screen support
 * to the basic CStdlibVK features.
 */
class CStdlibVKScreen : public CStdlibVK
{
public:
        CStdlibVKScreen(const char *kernel)
                : CStdlibVK (kernel),
                  mScreen (mOptions.GetWidth (), mOptions.GetHeight ()),
                  mTimer (&mInterrupt),
                  mLogger (mOptions.GetLogLevel (), &mTimer)
        {
        }

        virtual bool Initialize (void)
        {
                if (!CStdlibVK::Initialize ())
                {
                        return false;
                }

                if (!mScreen.Initialize ())
                {
                        return false;
                }

                CDevice *pTarget =
                        mDeviceNameService.GetDevice (mOptions.GetLogDevice (), false);
                if (pTarget == 0)
                {
                        pTarget = &mScreen;
                }

                if (!mLogger.Initialize (pTarget))
                {
                        return false;
                }

                return mTimer.Initialize ();
        }

protected:
        CScreenDevice   mScreen;
        CTimer          mTimer;
        CLogger         mLogger;
};

/**
 * Stdlib application that adds stdio support
 * to the CStdlibVKScreen functionality.
 */
class CStdlibVKStdio: public CStdlibVKScreen
{
private:
        char const *mpPartitionName;

public:
        // TODO transform to constexpr
        // constexpr char static DefaultPartition[] = "emmc1-1";
#define CSTDLIBAPP_LEGACY_DEFAULT_PARTITION "emmc1-1"
#define CSTDLIBAPP_DEFAULT_PARTITION "SD:"

        CStdlibVKStdio (const char *kernel,
                         const char *pPartitionName = CSTDLIBAPP_DEFAULT_PARTITION)
                : CStdlibVKScreen (kernel),
                  mpPartitionName (pPartitionName),
                  mUSBHCI (&mInterrupt, &mTimer, TRUE),
                  mEMMC (&mInterrupt, &mTimer, &mActLED),
#if !defined(__aarch64__) || !defined(LEAVE_QEMU_ON_HALT)
                  mConsole (&mNullDevice, &mScreen)
#else
                  mConsole (&mScreen)
#endif
        {
                assert (mpPartitionName != nullptr);
        }

        virtual bool Initialize (void)
        {
                if (!CStdlibVKScreen::Initialize ())
                {
                        return false;
                }

                if (!mEMMC.Initialize ())
                {
                        return false;
                }

                char const *partitionName = mpPartitionName;

                // Recognize the old default partition name
                if (strcmp(partitionName, CSTDLIBAPP_LEGACY_DEFAULT_PARTITION) == 0)
                {
                        partitionName = CSTDLIBAPP_DEFAULT_PARTITION;
                }

                if (f_mount (&mFileSystem, partitionName, 1) != FR_OK)
                {
                        mLogger.Write (GetKernelName (), LogError,
                                         "Cannot mount partition: %s", partitionName);

                        return false;
                }

                // USB-Initialisierung wurde nach CKernel::Initialize() verschoben,
                // damit Host vs. Gadget-Mode zur Laufzeit entschieden werden kann
                // (Config muss dafür bereits geladen sein).

                if (!mConsole.Initialize ())
                {
                        return false;
                }

                // Initialize newlib stdio with a reference to Circle's console
                CGlueStdioInit (mConsole);

                mLogger.Write (GetKernelName (), LogNotice, "Compile time: " __DATE__ " " __TIME__);

                return true;
        }

        virtual void Cleanup (void)
        {
                f_mount(0, "", 0);

                CStdlibVKScreen::Cleanup ();
        }

        const char *GetPartitionName() const
        {
                return mpPartitionName;
        }

protected:
        CUSBHCIDevice   mUSBHCI;
        CEMMCDevice     mEMMC;
        FATFS           mFileSystem;
        CConsole        mConsole;
};

/**
 * Stdlib application that adds network functionality
 * to the CStdlibVKStdio features.
 */
class CStdlibVKNetwork: public CStdlibVKStdio
{
public:
        #define CSTDLIBAPP_WLAN_FIRMWARE_PATH   CSTDLIBAPP_DEFAULT_PARTITION "/firmware/"
        #define CSTDLIBAPP_WLAN_CONFIG_FILE     CSTDLIBAPP_DEFAULT_PARTITION "/wpa_supplicant.conf"

        CStdlibVKNetwork (const char *kernel,
                   const char *pPartitionName = CSTDLIBAPP_DEFAULT_PARTITION,
                   const u8 *pIPAddress      = 0,       // use DHCP if pIPAddress == 0
                   const u8 *pNetMask        = 0,
                   const u8 *pDefaultGateway = 0,
                   const u8 *pDNSServer      = 0,
                   TNetDeviceType DeviceType = NetDeviceTypeEthernet)
          : CStdlibVKStdio(kernel, pPartitionName),
            mDeviceType (DeviceType),
            mWLAN (CSTDLIBAPP_WLAN_FIRMWARE_PATH),
            mNet(pIPAddress, pNetMask, pDefaultGateway, pDNSServer, DEFAULT_HOSTNAME, DeviceType),
            mWPASupplicant (CSTDLIBAPP_WLAN_CONFIG_FILE)
        {
        }

        virtual bool InitializeNoWaitForActivate (void)
        {
                if (!CStdlibVKStdio::Initialize ())
                {
                        return false;
                }

                if (mDeviceType == NetDeviceTypeWLAN)
                {
                        if (!mWLAN.Initialize ())
                        {
                                return false;
                        }
                }

                if (!mNet.Initialize (false))
                {
                        return false;
                }

                if (mDeviceType == NetDeviceTypeWLAN)
                {
                        if (!mWPASupplicant.Initialize ())
                        {
                                return false;
                        }
                }

                return true;
        }

        virtual bool Initialize (void)
        {
                if (!InitializeNoWaitForActivate ())
                {
                        return false;
                }

                while (!mNet.IsRunning ())
                {
                        mScheduler.Yield ();
                }

                // Initialize circle-newlib's network subsystem with a reference to Circle's
                // network subsytem.
                CGlueNetworkInit (mNet);

                return true;
        }

protected:
        CScheduler      mScheduler;
        TNetDeviceType  mDeviceType;
        CBcm4343Device  mWLAN;
        CNetSubSystem   mNet;
        CWPASupplicant  mWPASupplicant;
};
#endif
