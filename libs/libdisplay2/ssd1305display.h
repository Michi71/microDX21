#ifndef _ssd1305display_h
#define _ssd1305display_h

#include <circle/display.h>
#include <circle/i2cmaster.h>
#include <circle/types.h>

class CSSD1305Display : public CDisplay
{
public:
    CSSD1305Display(CI2CMaster* pI2C,
                    unsigned width,
                    unsigned height,
                    u8 address,
                    unsigned clock = 0);

    virtual ~CSSD1305Display();

    boolean Initialize();

    unsigned GetWidth(void) const  { return m_Width; }
    unsigned GetHeight(void) const { return m_Height; }
    unsigned GetDepth(void) const  { return 1; }

    void On(void);
    void Off(void);

    void Clear(TRawColor color = 0);
    void SetPixel(unsigned x, unsigned y, TRawColor color);
    void SetArea(const TArea& area, const void* pixels,
                 TAreaCompletionRoutine* routine, void* param);

private:
    boolean WriteCommand(u8 cmd);
    boolean WriteMemory(unsigned colStart, unsigned colEnd,
                        unsigned pageStart, unsigned pageEnd,
                        const void* data, size_t size);

private:
    CI2CMaster* m_pI2C;
    unsigned    m_Width;
    unsigned    m_Height;
    u8          m_Address;
    unsigned    m_Clock;

    u8 m_FB[8][128];
    unsigned m_ColOffset;
    unsigned m_PageOffset;
};

#endif
