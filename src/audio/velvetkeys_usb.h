#ifndef _VELVETKEYS_USB_H
#define _VELVETKEYS_USB_H

#if RASPPI >= 4

#include "velvetkeys.h"
#include <circle/sound/usbsoundbasedevice.h>

class CVelvetKeysUSB :
    public CVelvetKeys,
    public CUSBSoundBaseDevice
{
public:
    CVelvetKeysUSB(CConfig*          pConfig,
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
