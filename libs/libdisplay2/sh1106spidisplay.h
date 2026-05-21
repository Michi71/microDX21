// sh1106spidisplay.h
#ifndef _display_sh1106spidisplay_h
#define _display_sh1106spidisplay_h

#include <circle/display.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <circle/types.h>
#include <circle/timer.h>

class CSH1106SPIDisplay : public CDisplay
{
public:
    static const unsigned Width  = 128;
    static const unsigned Height = 64;

public:
    CSH1106SPIDisplay(CSPIMaster *pSPIMaster,
                      unsigned nWidth      = Width,
                      unsigned nHeight     = Height,
                      unsigned nDCPin      = 0,
                      unsigned nResetPin   = 0,
                      unsigned nClockSpeed = 8000000);

    ~CSH1106SPIDisplay(void);

    bool Initialize(void);

    unsigned GetWidth(void)  const { return m_nWidth; }
    unsigned GetHeight(void) const { return m_nHeight; }
    unsigned GetDepth(void)  const { return 1; }

    void On(void);
    void Off(void);

    void Clear(TRawColor nColor = 0);
    void SetPixel(unsigned nPosX, unsigned nPosY, TRawColor nColor);
    void SetArea(const TArea &rArea, const void *pPixels,
                 TAreaCompletionRoutine *pRoutine = nullptr,
                 void *pParam = nullptr);

private:
    bool WriteCommand(u8 uchCommand);
    bool WriteData(const void *pData, size_t ulDataSize);
    void HardwareReset(void);

private:
    CSPIMaster *m_pSPIMaster;
    unsigned    m_nWidth;
    unsigned    m_nHeight;
    unsigned    m_nClockSpeed;

    CGPIOPin    m_PinDC;
    CGPIOPin    m_PinReset;

    u8          m_Framebuffer[Height/8][Width];
};

#endif
