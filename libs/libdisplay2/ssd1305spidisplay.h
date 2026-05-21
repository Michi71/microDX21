#ifndef _display_ssd1305spidisplay_h
#define _display_ssd1305spidisplay_h

#include <circle/display.h>
#include <circle/spimaster.h>
#include <circle/gpiopin.h>
#include <circle/types.h>
#include <circle/timer.h>
#include <circle/logger.h>

class CSSD1305SPIDisplay : public CDisplay
{
public:
    static const unsigned None = GPIO_PINS;
    static const unsigned Width  = 128;
    static const unsigned Height = 32;

public:
    CSSD1305SPIDisplay(CSPIMaster *pSPIMaster,
                unsigned nDCPin , 
                unsigned nResetPin = None,
                unsigned nWidth = Width, 
                unsigned nHeight = Height,
                unsigned CPOL = 0, 
                unsigned CPHA = 0, 
                unsigned nClockSpeed = 8000000,
                unsigned nChipSelect = 0);

    ~CSSD1305SPIDisplay(void);

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

    void Show();

// private:
    void WriteCommand(u8 uchCommand);
    void WriteData (const void *pData, size_t nLength);
    bool WriteMemory(unsigned nColumnStart, unsigned nColumnEnd,
                     unsigned nPageStart,   unsigned nPageEnd,
                     const void *pData, size_t ulDataSize);
    void HardwareReset(void);

private:
    CSPIMaster *m_pSPIMaster;
    unsigned m_nResetPin;
    unsigned m_nWidth;
    unsigned m_nHeight;
    unsigned m_CPOL;
    unsigned m_CPHA;
    unsigned m_nClockSpeed;
    unsigned m_nChipSelect;

    CGPIOPin m_DCPin;
    CGPIOPin m_ResetPin;

    u8       m_Framebuffer[Width * Height / 8];
};

#endif
