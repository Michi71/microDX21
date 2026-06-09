#include "ssd1305display.h"
#include <string.h>

#define CMD  0x80
#define DATA 0x40

enum SSD1305Cmd : u8
{
    DisplayOff        = 0xAE,
    DisplayOn         = 0xAF,
    SetLowColumn      = 0x00,
    SetHighColumn     = 0x10,
    SetStartLine      = 0x40,
    SetContrast       = 0x81,
    SetBrightness     = 0x82,
    SetMultiplex      = 0xA8,
    MasterConfig      = 0xAD,
    SetRemap          = 0xA0,
    NormalDisplay     = 0xA6,
    InverseDisplay    = 0xA7,
    SetOffset         = 0xD3,
    SetClockDiv       = 0xD5,
    SetAreaColor      = 0xD8,
    SetPrecharge      = 0xD9,
    SetCompPins       = 0xDA,
    SetLUT            = 0x91,
    SetPageStart      = 0xB0
};

CSSD1305Display::CSSD1305Display(CI2CMaster* pI2C,
                                 unsigned width,
                                 unsigned height,
                                 u8 address,
                                 unsigned clock)
: CDisplay(I1)
, m_pI2C(pI2C)
, m_Width(width)
, m_Height(height)
, m_Address(address)
, m_Clock(clock)
{
    memset(m_FB, 0, sizeof(m_FB));

    if (height == 32)
    {
        m_PageOffset = 0;
        m_ColOffset  = 4;
    }
    else
    {
        m_PageOffset = 0;
        m_ColOffset  = 4;
    }
}

CSSD1305Display::~CSSD1305Display()
{
    WriteCommand(DisplayOff);
}

boolean CSSD1305Display::Initialize()
{
    static const u8 init_128x32[] =
    {
        DisplayOff,          // 0xAE
        0x04,                // Set Lower Column Start
        0x10,                // Set Higher Column Start
        0x40,                // Set Display Start Line
        SetContrast, 0x80,   // Contrast
        0xA1,                // Segment Remap
        NormalDisplay,       // 0xA6
        SetMultiplex, 0x1F,  // 32‑pixel height
        0xC8,                // COM Scan Direction
        SetOffset, 0x00,     // Display Offset
        SetClockDiv, 0xF0,   // Clock divide
        SetAreaColor, 0x05,  // Area color mode
        SetPrecharge, 0xC2,  // Precharge
        SetCompPins, 0x12,   // COM Pins
        0xDB, 0x3C,          // VCOMH
    };

    for (u8 c : init_128x32)
        if (!WriteCommand(c))
            return FALSE;

    WriteCommand(DisplayOn);
    return TRUE;
}

boolean CSSD1305Display::WriteCommand(u8 cmd)
{
    if (m_Clock)
        m_pI2C->SetClock(m_Clock);

    u8 buf[2] = { CMD, cmd };
    return m_pI2C->Write(m_Address, buf, 2) == 2;
}

void CSSD1305Display::On(void)
{
    WriteCommand(DisplayOn);
}

void CSSD1305Display::Off(void)
{
    WriteCommand(DisplayOff);
}

void CSSD1305Display::Clear(TRawColor color)
{
    memset(m_FB, color ? 0xFF : 0x00, sizeof(m_FB));
    WriteMemory(0, 127, 0, 3, m_FB, sizeof(m_FB));
}

void CSSD1305Display::SetPixel(unsigned x, unsigned y, TRawColor color)
{
    if (x >= m_Width || y >= m_Height)
        return;

    unsigned page = y / 8;
    unsigned bit  = y & 7;

    if (color)
        m_FB[page][x] |= (1 << bit);
    else
        m_FB[page][x] &= ~(1 << bit);

    WriteMemory(x, x, page, page, &m_FB[page][x], 1);
}

void CSSD1305Display::SetArea(const TArea& a, const void* pixels,
                              TAreaCompletionRoutine* r, void* p)
{
    unsigned w = a.x2 - a.x1 + 1;
    unsigned h = a.y2 - a.y1 + 1;
    unsigned bpr = (w + 7) / 8;

    const u8* src = (const u8*)pixels;

    for (unsigned y = 0; y < h; y++)
    {
        unsigned page = (a.y1 + y) / 8;
        unsigned bit  = (a.y1 + y) & 7;

        for (unsigned x = 0; x < w; x++)
        {
            bool pix = src[y * bpr + (x / 8)] & (0x80 >> (x & 7));

            if (pix)
                m_FB[page][a.x1 + x] |= (1 << bit);
            else
                m_FB[page][a.x1 + x] &= ~(1 << bit);
        }
    }

    WriteMemory(a.x1, a.x2, a.y1 / 8, a.y2 / 8, m_FB, sizeof(m_FB));

    if (r)
        r(p);
}

boolean CSSD1305Display::WriteMemory(unsigned colStart, unsigned colEnd,
                                     unsigned pageStart, unsigned pageEnd,
                                     const void* data, size_t size)
{
    if (m_Clock)
        m_pI2C->SetClock(m_Clock);

    for (unsigned page = pageStart; page <= pageEnd; page++)
    {
        unsigned col = colStart + m_ColOffset;

        WriteCommand(SetPageStart + page + m_PageOffset);
        WriteCommand(SetHighColumn | (col >> 4));
        WriteCommand(SetLowColumn  | (col & 0x0F));

        u8 buf[1 + (colEnd - colStart + 1)];
        buf[0] = DATA;

        memcpy(buf + 1, &m_FB[page][colStart], (colEnd - colStart + 1));

        if (m_pI2C->Write(m_Address, buf, sizeof(buf)) != (int)sizeof(buf))
            return FALSE;
    }

    return TRUE;
}
