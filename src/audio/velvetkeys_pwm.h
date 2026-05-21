#ifndef _VELVETKEYS_PWM_H
#define _VELVETKEYS_PWM_H

#include "velvetkeys.h"
#include <circle/sound/pwmsoundbasedevice.h>

class CVelvetKeysPWM :
    public CVelvetKeys,
    public CPWMSoundBaseDevice
{
public:
    CVelvetKeysPWM(CConfig*          pConfig,
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
