#include "ssd1305spidisplay.h"
#include <assert.h>
#include <string.h>

enum TSSD1305Command : u8
{
    SSD1305_SETCONTRAST         = 0x81,
    SSD1305_SETBRIGHTNESS       = 0x82,
    SSD1305_SETLUT              = 0x91,
    SSD1305_SEGREMAP            = 0xA0,
    SSD1305_DISPLAYALLON_RESUME = 0xA4,
    SSD1305_DISPLAYALLON        = 0xA5,
    SSD1305_NORMALDISPLAY       = 0xA6,
    SSD1305_INVERTDISPLAY       = 0xA7,
    SSD1305_SETMULTIPLEX        = 0xA8,
    SSD1305_DISPLAYDIM          = 0xAC,
    SSD1305_MASTERCONFIG        = 0xAD,
    SSD1305_DISPLAYOFF          = 0xAE,
    SSD1305_DISPLAYON           = 0xAF,
    SSD1305_SETPAGESTART        = 0xB0,
    SSD1305_COMSCANINC          = 0xC0,
    SSD1305_COMSCANDEC          = 0xC8,
    SSD1305_SETDISPLAYOFFSET    = 0xD3,
    SSD1305_SETDISPLAYCLOCKDIV  = 0xD5,
    SSD1305_SETAREACOLOR        = 0xD8,
    SSD1305_SETPRECHARGE        = 0xD9,
    SSD1305_SETCOMPINS          = 0xDA,
    SSD1305_SETVCOMLEVEL        = 0xDB,
    SSD1305_SETLOWCOLUMN        = 0x00,
    SSD1305_SETHIGHCOLUMN       = 0x10,
    SSD1305_SETSTARTLINE        = 0x40
};

static const unsigned SSD1305_Pages = CSSD1305SPIDisplay::Height / 8;

CSSD1305SPIDisplay::CSSD1305SPIDisplay(CSPIMaster *pSPIMaster,
                unsigned nDCPin, unsigned nResetPin,
                unsigned nWidth, unsigned nHeight,
                unsigned CPOL, unsigned CPHA, unsigned nClockSpeed,
                unsigned nChipSelect)
:   CDisplay (I1),
    m_pSPIMaster (pSPIMaster),
    m_nResetPin (nResetPin),
    m_nWidth (nWidth),
    m_nHeight (nHeight),
    m_CPOL (CPOL),
    m_CPHA (CPHA),
    m_nClockSpeed (nClockSpeed),
    m_nChipSelect (nChipSelect),
    m_DCPin (nDCPin, GPIOModeOutput)
{
    assert (nDCPin != None);

    if (m_nResetPin != None)
    {
        m_ResetPin.AssignPin (m_nResetPin);
        m_ResetPin.SetMode (GPIOModeOutput, FALSE);
    }

    memset(m_Framebuffer, 0, sizeof(m_Framebuffer));
}

CSSD1305SPIDisplay::~CSSD1305SPIDisplay(void)
{
    Off();
}

void CSSD1305SPIDisplay::HardwareReset(void)
{
    m_ResetPin.Write(HIGH);
    CTimer::SimpleMsDelay(100);
    m_ResetPin.Write(LOW);
    CTimer::SimpleMsDelay(100);
    m_ResetPin.Write(HIGH);
    CTimer::SimpleMsDelay(100);
}

bool CSSD1305SPIDisplay::Initialize(void)
{
    if (m_nWidth != Width || m_nHeight != Height)
        return false;

    assert(m_pSPIMaster != nullptr);

    HardwareReset();

    // Adafruit-Init für 128x32, leicht angepasst
    static const u8 init_128x32[] =
    {
        0xAE,0x04,0x10,0x40,0x81,0x80,0xA1,0xA6,
        0xA8,0x1F,0xC8,0xD3,0x00,0xD5,0xF0,0xD8,
        0x05,0xD9,0xC2,0xDA,0x12,0xDB,0x3C,0xAF,
    };

    for (unsigned i = 0; i < sizeof(init_128x32); ++i)
        WriteCommand(init_128x32[i]);

    CTimer::SimpleMsDelay(100);
    WriteCommand(SSD1305_DISPLAYON);

    Clear(0);
    return true;
}

void CSSD1305SPIDisplay::On(void)
{
    WriteCommand(SSD1305_DISPLAYON);
}

void CSSD1305SPIDisplay::Off(void)
{
    WriteCommand(SSD1305_DISPLAYOFF);
}

void CSSD1305SPIDisplay::Show()
{
    WriteMemory(0, m_nWidth - 1, 0, m_nHeight - 1,
                m_Framebuffer, sizeof(m_Framebuffer));
}

void CSSD1305SPIDisplay::Clear(TRawColor nColor)
{
    int i;
    for(i = 0;i<(int)sizeof(m_Framebuffer);i++)
    {
        m_Framebuffer[i] = 0;
    }
}

void CSSD1305SPIDisplay::SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor)
{
    if (nPosX >= m_nWidth || nPosY >= m_nHeight)
        return;
    if(nColor)
        m_Framebuffer[nPosX+(nPosY/8)*m_nWidth] |= 1<<(nPosY%8);
    else
        m_Framebuffer[nPosX+(nPosY/8)*m_nWidth] &= ~(1<<(nPosY%8));
}

void CSSD1305SPIDisplay::SetArea(const TArea &rArea, const void *pPixels,
                                 TAreaCompletionRoutine *pRoutine, void *pParam)
{
    assert(pPixels);

    unsigned width  = rArea.x2 - rArea.x1 + 1;
    unsigned height = rArea.y2 - rArea.y1 + 1;
    unsigned bytesPerRow = (width + 7) / 8;

    const u8 *src = (const u8 *)pPixels;

    for (unsigned y = 0; y < height; y++)
    {
        //unsigned page = (rArea.y1 + y) / 8;
        //unsigned bit  = (rArea.y1 + y) & 7;

        for (unsigned x = 0; x < width; x++)
        {
            bool pixel = src[(y * bytesPerRow) + (x / 8)] & (0x80 >> (x & 7));

            if (pixel)
                m_Framebuffer[x+(y/8)*m_nWidth] |= 1<<(y%8);
            else
                m_Framebuffer[x+(y/8)*m_nWidth] &= ~(1<<(y%8));
        }
    }

    Show();

    if (pRoutine)
        (*pRoutine)(pParam);
}

void CSSD1305SPIDisplay::WriteCommand(u8 uchCommand)
{
    assert (m_pSPIMaster != 0);

    m_DCPin.Write (LOW);

    m_pSPIMaster->SetClock (m_nClockSpeed);
    m_pSPIMaster->SetMode (m_CPOL, m_CPHA);

#ifndef NDEBUG
    int nResult =
#endif
        m_pSPIMaster->Write (m_nChipSelect, &uchCommand, sizeof uchCommand);
    assert (nResult == (int) sizeof uchCommand);
}

void CSSD1305SPIDisplay::WriteData (const void *pData, size_t nLength)
{
    assert (pData != 0);
    assert (nLength > 0);
    assert (m_pSPIMaster != 0);

    m_DCPin.Write (HIGH);

    m_pSPIMaster->SetClock (m_nClockSpeed);
    m_pSPIMaster->SetMode (m_CPOL, m_CPHA);

#ifndef NDEBUG
    int nResult =
#endif
        m_pSPIMaster->Write (m_nChipSelect, pData, nLength);
    assert (nResult == (int) nLength);
}


bool CSSD1305SPIDisplay::WriteMemory(unsigned /*nColumnStart*/, unsigned /*nColumnEnd*/,
                                     unsigned /*nPageStart*/,   unsigned /*nPageEnd*/,
                                     const void *pData, size_t /*ulDataSize*/)
{
    const u8 *buf = (const u8 *)pData;


    for (unsigned page = 0; page < (m_nHeight / 8); page++)
    {
        /* set page address */
        WriteCommand(0xB0 + page);
        /* set low column address */
        WriteCommand(0x04); 
        /* set high column address */
        WriteCommand(0x10); 

        /* write data */
        WriteData(buf + page * m_nWidth, m_nWidth);
    }

    return true;
}
