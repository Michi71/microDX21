#include "microdx21_i2s.h"
#include <circle/actled.h>
#include <circle/timer.h>
#include <circle/synchronize.h>
#include <circle/logger.h>

LOGMODULE("i2s");

CMicroDX21I2S::CMicroDX21I2S(CConfig*          pConfig,
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
, CI2SSoundBaseDevice(
        pInterrupt,
        pConfig->GetSampleRate(),
        pConfig->GetChunkSize(),
        false,
        pI2CMaster,
        pConfig->GetDACI2CAddress()
     )
{
}

bool CMicroDX21I2S::Initialize()
{
    // 1. Presets + MIDI devices FIRST (SD card I/O, no DMA conflict)
    if (!CMicroDX21::Initialize())
        return false;

    // 2. Then start I2S + DMA (audio output)
    if (!Start())
        return false;

    // 3. Log DAC controller info + LED diagnostic
    CSoundController* pCtrl = GetController();
    if (pCtrl)
    {
        LOGNOTE("I2S: DAC controller initialized OK");
        // 2 blinks = DAC detected (WM8960/PCM512x)
        for (int i = 0; i < 2; i++)
        {
            CActLED::Get()->On();
            CTimer::Get()->MsDelay(150);
            CActLED::Get()->Off();
            if (i < 1) CTimer::Get()->MsDelay(150);
        }
    }
    else
    {
        LOGNOTE("I2S: No DAC controller (assuming PCM5102A)");
        // 5 blinks = NO DAC detected (unconfigured WM8960!)
        for (int i = 0; i < 5; i++)
        {
            CActLED::Get()->On();
            CTimer::Get()->MsDelay(100);
            CActLED::Get()->Off();
            if (i < 4) CTimer::Get()->MsDelay(100);
        }
    }

    return true;
}

bool CMicroDX21I2S::Start()
{
    return CI2SSoundBaseDevice::Start();
}

bool CMicroDX21I2S::IsActive() const
{
    return CI2SSoundBaseDevice::IsActive();
}

unsigned CMicroDX21I2S::GetChunk(u32* pBuffer, unsigned nChunkSize)
{
    if (!pBuffer)
        return 0;

    unsigned nFrames = nChunkSize / 2;
    if (nFrames > 4096)
        nFrames = 4096;

    // HINWEIS: Diese Funktion wird vom DMA-Completion-IRQ aufgerufen,
    // läuft also bereits im IRQ-Context (IRQs sind hier deaktiviert).
    // Ein zusätzliches EnterCritical() würde nur FIQ blockieren und
    // andere kurze IRQ-Handler unnötig hinauszögern. Die NoteOn/
    // SetParameter-Pfade auf Core 0 schützen sich ihrerseits per
    // EnterCritical(), das genügt für Race-Freiheit gegenüber
    // processBlock().

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

    const float scale = 8388607.0f;

    for (unsigned i = 0; i < nFrames; ++i)
    {
        int32_t li = (int32_t)(m_outL[i] * scale);
        int32_t ri = (int32_t)(m_outR[i] * scale);

        if (m_bChannelsSwapped)
            std::swap(li, ri);

        *pBuffer++ = (u32)((uint32_t)li);
        *pBuffer++ = (u32)((uint32_t)ri);
    }

    return nFrames * 2;
}

