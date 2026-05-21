#include "ssd1306spidisplay.h"
#include <circle/util.h>
#include <assert.h>
#include <string.h>

enum TSSD1306Command : u8
{
    SetColumnAddress        = 0x21,
    SetPageAddress          = 0x22,
    SetStartLine            = 0x40,
    SetContrast             = 0x81,
    SetChargePump           = 0x8D,
    EntireDisplayOnResume   = 0xA4,
    SetNormalDisplay        = 0xA6,
    SetMultiplexRatio       = 0xA8,
    SetDisplayOff           = 0xAE,
    SetDisplayOn            = 0xAF,
    SetDisplayOffset        = 0xD3,
    SetDisplayClockDivide   = 0xD5,
    SetPrechargePeriod      = 0xD9,
    SetCOMPins              = 0xDA,
    SetVCOMHDeselectLevel   = 0xDB
};

CSSD1306SPIDisplay::CSSD1306SPIDisplay(CSPIMaster *pSPIMaster,
                                       unsigned nWidth,
                                       unsigned nHeight,
                                       unsigned nDCPin,
                                       unsigned nResetPin,
                                       unsigned nClockSpeed)
:   CDisplay(I1)
,   m_pSPIMaster(pSPIMaster)
,   m_nWidth(nWidth)
,   m_nHeight(nHeight)
,   m_nClockSpeed(nClockSpeed)
,   m_PinDC(nDCPin, GPIOModeOutput)
,   m_PinReset(nResetPin, GPIOModeOutput)
{
    m_PinDC.Write(LOW);
    m_PinReset.Write(HIGH);
}

CSSD1306SPIDisplay::~CSSD1306SPIDisplay(void)
{
    Off();
}

void CSSD1306SPIDisplay::HardwareReset(void)
{
    m_PinReset.Write(LOW);
    CTimer::SimpleMsDelay(10);
    m_PinReset.Write(HIGH);
    CTimer::SimpleMsDelay(10);
}

bool CSSD1306SPIDisplay::Initialize(void)
{
    if (m_nWidth != 128 || m_nHeight != 64)
        return false;

    HardwareReset();

    const u8 InitSequence[] =
    {
        SetDisplayOff,
        SetDisplayClockDivide, 0x80,
        SetMultiplexRatio,     0x3F,
        SetDisplayOffset,      0x00,
        SetStartLine | 0x00,
        SetChargePump,         0x14,
        0xA1,                  // Segment remap
        0xC8,                  // COM scan direction
        SetCOMPins,            0x12,
        SetContrast,           0x7F,
        SetPrechargePeriod,    0x22,
        SetVCOMHDeselectLevel, 0x20,
        EntireDisplayOnResume,
        SetNormalDisplay,
        SetDisplayOn
    };

    for (u8 cmd : InitSequence)
        if (!WriteCommand(cmd))
            return false;

    Clear();
    return true;
}

void CSSD1306SPIDisplay::On(void)
{
    WriteCommand(SetDisplayOn);
}

void CSSD1306SPIDisplay::Off(void)
{
    WriteCommand(SetDisplayOff);
}

void CSSD1306SPIDisplay::Clear(TRawColor nColor)
{
    size_t size = m_nWidth * m_nHeight / 8;
    memset(m_Framebuffer, nColor ? 0xFF : 0x00, size);

    WriteCommand(SetColumnAddress);
    WriteCommand(0);
    WriteCommand(m_nWidth - 1);

    WriteCommand(SetPageAddress);
    WriteCommand(0);
    WriteCommand((m_nHeight / 8) - 1);

    for (unsigned page = 0; page < m_nHeight/8; page++)
        WriteData(m_Framebuffer[page], m_nWidth);
}

void CSSD1306SPIDisplay::SetPixel(unsigned x, unsigned y, TRawColor color)
{
    if (x >= m_nWidth || y >= m_nHeight)
        return;

    u8 page = y / 8;
    u8 mask = 1 << (y & 7);

    if (color)
        m_Framebuffer[page][x] |= mask;
    else
        m_Framebuffer[page][x] &= ~mask;

    WriteCommand(SetColumnAddress);
    WriteCommand(x);
    WriteCommand(x);

    WriteCommand(SetPageAddress);
    WriteCommand(page);
    WriteCommand(page);

    WriteData(&m_Framebuffer[page][x], 1);
}

void CSSD1306SPIDisplay::SetArea(const TArea &rArea, const void *pPixels,
                                 TAreaCompletionRoutine *pRoutine, void *pParam)
{
    assert(pPixels);

    unsigned width  = rArea.x2 - rArea.x1 + 1;
    unsigned height = rArea.y2 - rArea.y1 + 1;
    unsigned bytesPerRow = (width + 7) / 8;

    const u8 *src = (const u8 *)pPixels;

    for (unsigned y = 0; y < height; y++)
    {
        unsigned page = (rArea.y1 + y) / 8;
        unsigned bit  = (rArea.y1 + y) & 7;

        for (unsigned x = 0; x < width; x++)
        {
            bool pixel = src[(y * bytesPerRow) + (x / 8)] & (0x80 >> (x & 7));

            if (pixel)
                m_Framebuffer[page][rArea.x1 + x] |= (1 << bit);
            else
                m_Framebuffer[page][rArea.x1 + x] &= ~(1 << bit);
        }
    }

    WriteCommand(SetColumnAddress);
    WriteCommand(rArea.x1);
    WriteCommand(rArea.x2);

    WriteCommand(SetPageAddress);
    WriteCommand(rArea.y1 / 8);
    WriteCommand(rArea.y2 / 8);

    for (unsigned page = rArea.y1/8; page <= rArea.y2/8; page++)
        WriteData(&m_Framebuffer[page][rArea.x1],
                  rArea.x2 - rArea.x1 + 1);

    if (pRoutine)
        (*pRoutine)(pParam);
}

bool CSSD1306SPIDisplay::WriteCommand(u8 cmd)
{
    if (m_nClockSpeed)
        m_pSPIMaster->SetClock(m_nClockSpeed);

    m_PinDC.Write(LOW);

    int res = m_pSPIMaster->Write(0, &cmd, 1);

    return res == 1;
}

bool CSSD1306SPIDisplay::WriteData(const void *pData, size_t size)
{
    if (m_nClockSpeed)
        m_pSPIMaster->SetClock(m_nClockSpeed);

    m_PinDC.Write(HIGH);

    int res = m_pSPIMaster->Write(0, pData, size);

    return res == (int) size;
}
