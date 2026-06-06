#ifndef _MICRODX21_USB_H
#define _MICRODX21_USB_H

#if RASPPI >= 4

#include "microdx21.h"
#include <circle/sound/usbsoundbasedevice.h>

class CMicroDX21USB :
    public CMicroDX21,
    public CUSBSoundBaseDevice
{
public:
    CMicroDX21USB(CConfig*          pConfig,
                  CInterruptSystem* pInterrupt,
                  CGPIOManager*     pGPIOManager,
                  CI2CMaster*       pI2CMaster,
                  FATFS*            pFileSystem);

    bool Initialize();
    bool Start();
    bool IsActive() const;

private:
    unsigned GetChunk(u32* pBuffer, unsigned nChunkSize) override;

    float m_outL[4096];
    float m_outR[4096];
};

#endif

#endif
