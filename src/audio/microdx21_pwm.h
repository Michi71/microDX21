#ifndef _MICRODX21_PWM_H
#define _MICRODX21_PWM_H

#include "microdx21.h"
#include <circle/sound/pwmsoundbasedevice.h>

class CMicroDX21PWM :
    public CMicroDX21,
    public CPWMSoundBaseDevice
{
public:
    CMicroDX21PWM(CConfig*          pConfig,
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
