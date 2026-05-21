#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <circle/types.h>
#include <circle/i2cmaster.h>
#include <circle/spimaster.h>
#include <circle/gpiomanager.h>
#include <circle/display.h>
#include <circle/screen.h>
#include <lvgl/lvgl.h>
#include <atomic>

#include "displayconfig.h"
#include "display_registry.h"
#include <sensor/ky040.h>
#include <gt1151touch.h>

#include "guicontroller.h"
#include "screen_splash.h"
#include "screen_presetbrowser.h"
#include "velvetkeys.h"

class CDisplayManager
{
public:
    CDisplayManager(CI2CMaster*      pI2C,
                    CSPIMaster*      pSPI,
                    CGPIOManager*    pGPIO,
                    const DisplayConfig& cfg,
                    CScreenDevice*   pScreen = nullptr);

    bool Initialize();
    void Process();
    void ProcessDisplayThread();
    void ProcessEncoderEvents();

    bool IsReady() const { return m_bReady; }

    void SetVelvetKeys(CVelvetKeys* vk) { m_VelvetKeys = vk; }
    CVelvetKeys* GetVelvetKeys() const { return m_VelvetKeys; }

private:
    bool DetectI2CDisplay();
    bool DetectSPIDisplay();
    bool InitDisplay();

    void OnEncoderEvent(CKY040::TEvent ev);

private:
    CI2CMaster*    m_pI2C;
    CSPIMaster*    m_pSPI;
    CGPIOManager*  m_pGPIOManager;
    DisplayConfig  m_Config;

    CDisplay*      m_pDisplay   = nullptr;
    CLVGL*         m_pLVGL      = nullptr;
    CScreenDevice* m_pScreen    = nullptr;

    CKY040         m_Encoder;

    GUIController* m_GUI        = nullptr;

    bool           m_bMonoDisp  = false;
    bool           m_bReady     = false;

    CVelvetKeys*   m_VelvetKeys = nullptr;

    std::atomic<bool> m_encoderBackPending{false};

    // Touch (GT1151)
    CGT1151TouchScreen* m_pTouch = nullptr;
};

#endif // DISPLAY_MANAGER_H
