#include "sh1106display.h"
#include <circle/util.h>
#include <assert.h>
#include <string.h>

#define DATA 0x40
#define CMD  0x80

enum TSH1106Command : u8
{
    SetColumnAddressLow   = 0x00,
    SetColumnAddressHigh  = 0x10,
    SetPageAddress        = 0xB0,
    SetStartLine          = 0x40,
    SetContrast           = 0x81,
    SetChargePump         = 0x8D,
    EntireDisplayOnResume = 0xA4,
    SetNormalDisplay      = 0xA6,
    SetInverseDisplay     = 0xA7,
    SetMultiplexRatio     = 0xA8,
    SetDisplayOff         = 0xAE,
    SetDisplayOn          = 0xAF,
    SetDisplayOffset      = 0xD3,
    SetDisplayClockDivideRatio = 0xD5,
    SetPrechargePeriod    = 0xD9,
    SetCOMPins            = 0xDA,
    SetVCOMHDeselectLevel = 0xDB
};

CSH1106Display::CSH1106Display(CI2CMaster *pI2CMaster,
                               unsigned nWidth, unsigned nHeight,
                               u8 uchI2CAddress, unsigned nClockSpeed)
:   CDisplay(I1),
    m_pI2CMaster(pI2CMaster),
    m_nWidth(nWidth),
    m_nHeight(nHeight),
    m_uchI2CAddress(uchI2CAddress),
    m_nClockSpeed(nClockSpeed)
{
}

CSH1106Display::~CSH1106Display(void)
{
    Off();
}

boolean CSH1106Display::Initialize(void)
{
    if (m_nWidth != 128 || m_nHeight != 64)
        return FALSE;

    const u8 InitSequence[] =
    {
        SetDisplayOff,
        SetDisplayClockDivideRatio, 0x80,
        SetMultiplexRatio,          0x3F,
        SetDisplayOffset,           0x00,
        SetStartLine | 0x00,
        SetChargePump,              0x14,
        0xA1,                       // Segment remap
        0xC8,                       // COM scan direction
        SetCOMPins,                 0x12,
        SetContrast,                0x7F,
        SetPrechargePeriod,         0x22,
        SetVCOMHDeselectLevel,      0x20,
        EntireDisplayOnResume,
        SetNormalDisplay,
        SetDisplayOn
    };

    for (u8 cmd : InitSequence)
        if (!WriteCommand(cmd))
            return FALSE;

    Clear();

    return TRUE;
}

void CSH1106Display::On(void)
{
    WriteCommand(SetDisplayOn);
}

void CSH1106Display::Off(void)
{
    WriteCommand(SetDisplayOff);
}

void CSH1106Display::Clear(TRawColor nColor)
{
    size_t size = m_nWidth * m_nHeight / 8;
    memset(m_Framebuffer, nColor ? 0xFF : 0x00, size);

    WriteMemory(0, m_nWidth - 1, 0, (m_nHeight / 8) - 1,
                m_Framebuffer, size);
}

void CSH1106Display::SetPixel(unsigned x, unsigned y, TRawColor color)
{
    if (x >= m_nWidth || y >= m_nHeight)
        return;

    u8 page = y / 8;
    u8 mask = 1 << (y & 7);

    if (color)
        m_Framebuffer[page][x] |= mask;
    else
        m_Framebuffer[page][x] &= ~mask;

    WriteMemory(x, x, page, page, &m_Framebuffer[page][x], 1);
}

void CSH1106Display::SetArea(const TArea &rArea, const void *pPixels,
                             TAreaCompletionRoutine *pRoutine, void *pParam)
{
    assert(pPixels);

    unsigned width = rArea.x2 - rArea.x1 + 1;
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

    WriteMemory(rArea.x1, rArea.x2,
                rArea.y1 / 8, rArea.y2 / 8,
                m_Framebuffer, sizeof(m_Framebuffer));

    if (pRoutine)
        (*pRoutine)(pParam);
}

boolean CSH1106Display::WriteCommand(u8 cmd)
{
    if (m_nClockSpeed)
        m_pI2CMaster->SetClock(m_nClockSpeed);

    u8 buf[] = { CMD, cmd };
    return m_pI2CMaster->Write(m_uchI2CAddress, buf, sizeof(buf)) == sizeof(buf);
}

boolean CSH1106Display::WriteMemory(unsigned colStart, unsigned colEnd,
                                    unsigned pageStart, unsigned pageEnd,
                                    const void *pData, size_t size)
{
    if (m_nClockSpeed)
        m_pI2CMaster->SetClock(m_nClockSpeed);

    for (unsigned page = pageStart; page <= pageEnd; page++)
    {
        // SH1106 visible area offset = 2
        unsigned column = colStart + 2;

        WriteCommand(SetColumnAddressLow  | (column & 0x0F));
        WriteCommand(SetColumnAddressHigh | ((column >> 4) & 0x0F));

        WriteCommand(SetPageAddress | page);

        u8 buffer[1 + (colEnd - colStart + 1)];
        buffer[0] = DATA;

        memcpy(buffer + 1,
               &m_Framebuffer[page][colStart],
               colEnd - colStart + 1);

        if (m_pI2CMaster->Write(m_uchI2CAddress, buffer, sizeof(buffer)) != (int) sizeof(buffer))
            return FALSE;
    }

    return TRUE;
}
