#include "microdx21_pwm.h"
#include <circle/synchronize.h>

CMicroDX21PWM::CMicroDX21PWM(CConfig*          pConfig,
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
, CPWMSoundBaseDevice(
        pInterrupt,
        pConfig->GetSampleRate(),
        pConfig->GetChunkSize()
     )
{
}

bool CMicroDX21PWM::Initialize()
{
    if (!CMicroDX21::Initialize())
        return false;

    if (!Start())
        return false;

    return true;
}

bool CMicroDX21PWM::Start()
{
    return CPWMSoundBaseDevice::Start();
}

bool CMicroDX21PWM::IsActive() const
{
    return CPWMSoundBaseDevice::IsActive();
}

unsigned CMicroDX21PWM::GetChunk(u32* pBuffer, unsigned nChunkSize)
{
    if (!pBuffer)
        return 0;

    unsigned nFrames = nChunkSize / 2;
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

    float max = (float)GetRangeMax();
    float min = (float)GetRangeMin();
    float nullLevel = (GetRangeMin() + GetRangeMax()) * 0.5f;

    float scale = (max - min) / 2.0f;

    for (unsigned i = 0; i < nFrames; ++i)
       {
        float l = m_outL[i] * scale + nullLevel;
        float r = m_outR[i] * scale + nullLevel;

        int32_t li = (int32_t)l;
        int32_t ri = (int32_t)r;

        if (m_bChannelsSwapped)
            std::swap(li, ri);

          *pBuffer++ = (u32)li;
          *pBuffer++ = (u32)ri;
       }

    LeaveCritical();

    return nFrames * 2;
}

