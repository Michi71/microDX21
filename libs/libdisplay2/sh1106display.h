#ifndef _display_sh1106display_h
#define _display_sh1106display_h

#include <circle/display.h>
#include <circle/i2cmaster.h>
#include <circle/types.h>

class CSH1106Display : public CDisplay
{
public:
    static const unsigned Width = 128;
    static const unsigned Height = 64;

public:
    CSH1106Display(CI2CMaster *pI2CMaster,
                   unsigned nWidth = Width,
                   unsigned nHeight = Height,
                   u8 uchI2CAddress = 0x3C,
                   unsigned nClockSpeed = 0);

    ~CSH1106Display(void);

    boolean Initialize(void);

    unsigned GetWidth(void) const  { return m_nWidth; }
    unsigned GetHeight(void) const { return m_nHeight; }
    unsigned GetDepth(void) const  { return 1; }

    void On(void);
    void Off(void);

    void Clear(TRawColor nColor = 0);
    void SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor);
    void SetArea(const TArea &rArea, const void *pPixels,
                 TAreaCompletionRoutine *pRoutine = nullptr,
                 void *pParam = nullptr);

private:
    boolean WriteCommand(u8 uchCommand);
    boolean WriteMemory(unsigned nColumnStart, unsigned nColumnEnd,
                        unsigned nPageStart, unsigned nPageEnd,
                        const void *pData, size_t ulDataSize);

private:
    CI2CMaster *m_pI2CMaster;
    unsigned m_nWidth;
    unsigned m_nHeight;
    u8 m_uchI2CAddress;
    unsigned m_nClockSpeed;

    // SH1106 has 132 columns of RAM, but we only use 128
    u8 m_Framebuffer[Height/8][Width];
};

#endif
