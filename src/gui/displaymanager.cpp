#include "displaymanager.h"
#include <circle/logger.h>
#include "style_mono.h"
#include "encoder.h"

static const char FromDisplay[] = "display";

// Globaler Pointer für den Touch-Treiber (wird vom LVGL-Callback genutzt)
static CGT1151TouchScreen* g_pTouchScreen = nullptr;

// LVGL Touch-Read-Callback (nicht static – wird von guicontroller.cpp referenziert)
void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    if (g_pTouchScreen)
    {
        int x, y;
        boolean touched;
        if (g_pTouchScreen->Read(&x, &y, &touched) && touched)
        {
            data->point.x = x;
            data->point.y = y;
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}

CDisplayManager::CDisplayManager(CI2CMaster*      pI2C,
                                 CSPIMaster*      pSPI,
                                 CGPIOManager*    pGPIO,
                                 const DisplayConfig& cfg,
                                 CScreenDevice*   pScreen)
: m_pI2C(pI2C)
, m_pSPI(pSPI)
, m_pGPIOManager(pGPIO)
, m_Config(cfg)
, m_pScreen(pScreen)
, m_Encoder(cfg.encPinA, cfg.encPinB, cfg.encPinBtn, pGPIO)
{
    CLogger::Get()->Write(FromDisplay, LogNotice, "Constructor: starting");
}

bool CDisplayManager::DetectI2CDisplay()
{
    CLogger::Get()->Write(FromDisplay, LogNotice, "DetectI2CDisplay: starting");
    if (!m_pI2C)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "DetectI2CDisplay: no I2C master");
        return false;
    }

    for (u8 addr : {0x3C, 0x3D})
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "DetectI2CDisplay: checking address 0x%02X", addr);
        if (m_pI2C->Write(addr, nullptr, 0) >= 0)
        {
            CLogger::Get()->Write(FromDisplay, LogNotice,
                                  "Detected I2C OLED at 0x%02X", addr);
            m_Config.i2cAddress = addr;
            return true;
        }
    }

    CLogger::Get()->Write(FromDisplay, LogNotice, "DetectI2CDisplay: no I2C display found");
    return false;
}

bool CDisplayManager::DetectSPIDisplay()
{
    CLogger::Get()->Write(FromDisplay, LogNotice, "DetectSPIDisplay: starting");
    if (!m_pSPI)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "DetectSPIDisplay: no SPI master");
        return false;
    }

    if (m_Config.pinDC == 0)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "DetectSPIDisplay: no DC pin configured");
        return false;
    }

    CLogger::Get()->Write(FromDisplay, LogNotice, "DetectSPIDisplay: creating GPIO pin for DC");
    CGPIOPin dc(m_Config.pinDC, GPIOModeOutput, m_pGPIOManager);

    dc.Write(LOW);

    uint8_t dummy = 0x00;
    CLogger::Get()->Write(FromDisplay, LogNotice, "DetectSPIDisplay: testing SPI write");
    int res = m_pSPI->Write(0, &dummy, 1);

    if (res >= 0)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice,
                              "SPI display likely present (DC=%u)",
                              m_Config.pinDC);
        return true;
    }

    CLogger::Get()->Write(FromDisplay, LogNotice, "DetectSPIDisplay: no SPI display found");
    return false;
}

bool CDisplayManager::InitDisplay()
{
    CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: starting");
    
    if (m_Config.bus == DisplayConfig::Bus::HDMI)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: HDMI mode");
        if (!m_pScreen)
        {
            CLogger::Get()->Write(FromDisplay, LogError,
                                  "HDMI selected but no CScreenDevice provided");
            return false;
        }

        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: creating CLVGL");
        m_pLVGL = new CLVGL(m_pScreen);
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: initializing CLVGL");
        if (!m_pLVGL->Initialize())
        {
            CLogger::Get()->Write(FromDisplay, LogError, "LVGL init failed (HDMI)");
            return false;
        }
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: CLVGL initialized");

        m_pDisplay = m_pScreen->GetFrameBuffer();
    }
    else
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: non-HDMI mode, creating display via registry");
        m_pDisplay = DisplayRegistry::Instance().Create(m_Config, m_pI2C, m_pSPI);

        if (!m_pDisplay)
        {
            CLogger::Get()->Write(FromDisplay, LogError,
                                  "No matching display driver found");
            return false;
        }
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: display driver created");

        bool ok = true;

        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: initializing display hardware");
        if (auto* p = dynamic_cast<CSH1106Display*>(m_pDisplay)) {
            CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: detected CSH1106Display");
            ok = p->Initialize();
            m_bMonoDisp = true;
        }
        else if (auto* p = dynamic_cast<CSSD1305Display*>(m_pDisplay)) {
            CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: detected CSSD1305Display");
            ok = p->Initialize();
            m_bMonoDisp = true;
        }
        else if (auto* p = dynamic_cast<CSSD1306Display*>(m_pDisplay)) {
            CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: detected CSSD1306Display");
            ok = p->Initialize();
            m_bMonoDisp = true;
        }
        else if (auto* p = dynamic_cast<CSH1106SPIDisplay*>(m_pDisplay)) {
            CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: detected CSH1106SPIDisplay");
            ok = p->Initialize();
            m_bMonoDisp = true;
        }
        else if (auto* p = dynamic_cast<CSSD1305SPIDisplay*>(m_pDisplay)) {
            CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: detected CSSD1305SPIDisplay");
            ok = p->Initialize();
            m_bMonoDisp = true;
        }
        else if (auto* p = dynamic_cast<CSSD1306SPIDisplay*>(m_pDisplay)) {
            CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: detected CSSD1306SPIDisplay");
            ok = p->Initialize();
            m_bMonoDisp = true;
        }

        if (!ok)
        {
            CLogger::Get()->Write(FromDisplay, LogError, "InitDisplay: display hardware init failed");
            return false;
        }
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: display hardware initialized");

        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: creating CLVGL");
        m_pLVGL = new CLVGL(m_pDisplay);
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: initializing CLVGL");
        if (!m_pLVGL->Initialize())
        {
            CLogger::Get()->Write(FromDisplay, LogError, "LVGL init failed");
            return false;
        }
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: CLVGL initialized");
    }

    if (m_bMonoDisp)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: initializing mono styles");
        vk_init_mono_styles();
        CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: mono styles initialized");
    }

    CLogger::Get()->Write(FromDisplay, LogNotice, "InitDisplay: complete");
    return true;
}

bool CDisplayManager::Initialize()
{
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: starting");
    
    if (!m_Config.enabled)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice,
                              "Display disabled in config");
        return true;
    }

    // Encoder initialisieren
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: initializing encoder");
    m_Encoder.Initialize();
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: encoder initialized");
    
    m_Encoder.RegisterEventHandler([](CKY040::TEvent ev, void* param){
        auto* self = static_cast<CDisplayManager*>(param);
        self->OnEncoderEvent(ev);
    }, this);
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: encoder event handler registered");

    // Auto-Bus-Erkennung
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: starting bus detection");
    if (m_Config.bus == DisplayConfig::Bus::Auto)
    {
        if (DetectI2CDisplay())
            m_Config.bus = DisplayConfig::Bus::I2C;
        else if (DetectSPIDisplay())
            m_Config.bus = DisplayConfig::Bus::SPI;
        else
            m_Config.bus = DisplayConfig::Bus::HDMI;
    }
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: bus detection complete");

    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: calling InitDisplay()");
    if (!InitDisplay())
    {
        CLogger::Get()->Write(FromDisplay, LogWarning, "Initialize: InitDisplay() failed, continuing without display");
        // Continue without display – do NOT create GUI or LVGL objects
        m_bReady = true;
        return true;
    }
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: InitDisplay() completed");

    unsigned width  = 0;
    unsigned height = 0;
    DisplayType type = DisplayType::Mono128x64;

    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: determining display dimensions");
    if (m_Config.bus == DisplayConfig::Bus::HDMI && m_pScreen)
    {
        width  = m_pScreen->GetWidth();
        height = m_pScreen->GetHeight();
        type = DisplayType::HDMI;
    }
    else if (m_pDisplay)
    {
        width  = m_pDisplay->GetWidth();
        height = m_pDisplay->GetHeight();

        if ((m_bMonoDisp == true) && (height == 32))
            type = DisplayType::Mono128x32;
        else if ((m_bMonoDisp == true) && (height == 64))
            type = DisplayType::Mono128x64;
        else if ((m_bMonoDisp == false) && (height == 240) && (width == 320))
            type = DisplayType::Color320x240;
    }
    else
    {
        // No display available – should not reach here because InitDisplay() failed above
        width = 128;
        height = 64;
        type = DisplayType::Mono128x64;
        CLogger::Get()->Write(FromDisplay, LogWarning, "Initialize: no display available, using default dimensions");
    }
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: display dimensions: %ux%u", width, height);

    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: creating GUIController");
    m_GUI = new GUIController(width, height, type);
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: GUIController created");

    // VelvetKeys MUSS VOR Init() gesetzt werden
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: setting VelvetKeys");
    m_GUI->SetVelvetKeys(m_VelvetKeys);
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: calling m_GUI->Init()");
    m_GUI->Init();
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: m_GUI->Init() completed");

    // Erst jetzt dürfen Screens erzeugt werden
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: creating ScreenSplash");
    m_GUI->ShowScreen(new ScreenSplash(type));
    CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: ScreenSplash created and shown");

    // Touch initialisieren (nur wenn Touch-Pins konfiguriert)
    if (m_Config.touchResetPin != 0 && m_pI2C)
    {
        CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: initializing touch");
        m_pTouch = new CGT1151TouchScreen(
            m_pI2C,
            m_pGPIOManager,
            m_Config.touchResetPin,
            m_Config.touchIRQPin,
            width,
            height,
            (u8)m_Config.touchI2CAddress
        );
        if (m_pTouch->Initialize())
        {
            m_pTouch->SetRotation(m_Config.rotation);
            g_pTouchScreen = m_pTouch;
            m_GUI->InitTouchInput();
            CLogger::Get()->Write(FromDisplay, LogNotice, "Initialize: touch initialized");
        }
        else
        {
            CLogger::Get()->Write(FromDisplay, LogWarning, "Initialize: touch init failed");
            delete m_pTouch;
            m_pTouch = nullptr;
            g_pTouchScreen = nullptr;
        }
    }

    m_bReady = true;
    CLogger::Get()->Write(FromDisplay, LogNotice, "DisplayManager ready");
    return true;
}

void CDisplayManager::Process()
{
    if (!m_bReady)
        return;
}

void CDisplayManager::ProcessDisplayThread()
{
    if (!m_bReady)
        return;

    if (m_pLVGL)
        m_pLVGL->Update();

    if (m_GUI)
        m_GUI->Update();
}

void CDisplayManager::OnEncoderEvent(CKY040::TEvent ev)
{
    switch (ev)
    {
        case CKY040::EventClockwise:
            if (!get_encoder_block_right())
                encoder_hw_delta(+1);
            break;

        case CKY040::EventCounterclockwise:
            if (!get_encoder_block_left())
                encoder_hw_delta(-1);
            break;

        case CKY040::EventSwitchClick:
            encoder_hw_click();
            break;

        case CKY040::EventSwitchHold:
            // Deferred: LVGL must not be called from IRQ/timer context
            m_encoderBackPending.store(true, std::memory_order_release);
            break;

        default:
            break;
    }
}

void CDisplayManager::ProcessEncoderEvents()
{
    if (!m_bReady)
        return;

    if (m_encoderBackPending.exchange(false, std::memory_order_acq_rel))
    {
        if (m_GUI)
            m_GUI->OnEncoderBack();
    }
}

