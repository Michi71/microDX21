#include "microdx21_usb.h"
#include <circle/sound/usbsoundbasedevice.h>
#include <circle/synchronize.h>

#if RASPPI >= 4

CMicroDX21USB::CMicroDX21USB(CConfig*          pConfig,
                               CInterruptSystem* pInterrupt,
                               CGPIOManager*     pGPIOManager,
                               CI2CMaster*       pI2CMaster,
                               FATFS*            pFileSystem)
: CMicroDX21(pConfig,
              pInterrupt,
              pGPIOManager,
              pI2CMaster,
              nullptr,
              pFileSystem)
, CUSBSoundBaseDevice(
        pConfig->GetSampleRate()
      )
{
}

bool CMicroDX21USB::Initialize()
{
    if (!CMicroDX21::Initialize())
        return false;

    if (!Start())
        return false;

    return true;
}

bool CMicroDX21USB::Start()
{
    return CUSBSoundBaseDevice::Start();
}

bool CMicroDX21USB::IsActive() const
{
    return CUSBSoundBaseDevice::IsActive();
}

unsigned CMicroDX21USB::GetChunk(u32* pBuffer, unsigned nChunkSize)
{
    if (!pBuffer)
        return 0;

    unsigned nChannels = GetHWTXChannels();
    if (nChannels == 0)
        return 0;

    unsigned nFrames   = nChunkSize / nChannels;

    if (nFrames > 4096)
        nFrames = 4096;

    EnterCritical();

#ifdef ARM_ALLOW_MULTI_CORE
    // Multicore: Audio kommt aus dem Double-Buffer (Core 1)
    unsigned gotFrames = 0;
    GetAudioChunk(m_outL, m_outR, gotFrames);
    if (gotFrames > nFrames)
        gotFrames = nFrames;
    nFrames = gotFrames;
#else
    // Single-core: GenerateAudio direkt aufrufen
    nFrames = GenerateAudio(m_outL, m_outR, nFrames);
#endif

    float scale = 8388607.0f; // 24-bit max

    for (unsigned f = 0; f < nFrames; ++f)
    {
        int32_t li = (int32_t)(m_outL[f] * scale);
        int32_t ri = (int32_t)(m_outR[f] * scale);

        if (m_bChannelsSwapped)
            std::swap(li, ri);

        // 24-bit in 32-bit container (right-justified, standard USB Audio)
        *pBuffer++ = (u32)li;
        *pBuffer++ = (u32)ri;
    }

    LeaveCritical();

    return nFrames * nChannels;
}


#endif
