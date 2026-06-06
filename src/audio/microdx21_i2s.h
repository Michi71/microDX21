#ifndef _MICRODX21_I2S_H
#define _MICRODX21_I2S_H

#include "microdx21.h"
#include <circle/sound/i2ssoundbasedevice.h>

class CMicroDX21I2S :
    public CMicroDX21,
    public CI2SSoundBaseDevice
{
public:
    CMicroDX21I2S(CConfig*          pConfig,
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
