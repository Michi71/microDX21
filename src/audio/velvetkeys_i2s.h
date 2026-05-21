#ifndef _VELVETKEYS_I2S_H
#define _VELVETKEYS_I2S_H

#include "velvetkeys.h"
#include <circle/sound/i2ssoundbasedevice.h>

class CVelvetKeysI2S :
    public CVelvetKeys,
    public CI2SSoundBaseDevice
{
public:
    CVelvetKeysI2S(CConfig*          pConfig,
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
