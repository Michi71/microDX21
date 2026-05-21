//
// velvetkeys_multicore.cpp
//
#ifdef ARM_ALLOW_MULTI_CORE

#include "velvetkeys_multicore.h"
#include "kernel.h"
#include <circle/logger.h>
#include <circle/memory.h>

CVelvetKeysMultiCore::CVelvetKeysMultiCore(CKernel *pKernel)
: CMultiCoreSupport(CMemorySystem::Get())
, m_pKernel(pKernel)
{
}

void CVelvetKeysMultiCore::Run(unsigned nCore)
{
    switch (nCore)
    {
        case 1:
            CLogger::Get()->Write("multicore", LogNotice,
                                    "Core 1: Audio Prebuffer (Double-Buffer)");
            m_pKernel->RunCore1();
            break;

        case 2:
            CLogger::Get()->Write("multicore", LogNotice,
                                    "Core 2: Display Manager (LVGL + Encoder)");
            m_pKernel->RunCore2();
            break;

        case 3:
            CLogger::Get()->Write("multicore", LogNotice,
                                    "Core 3: Audio Prep (Fallback/Idle)");
            m_pKernel->RunCore3();
            break;

        default:
            CLogger::Get()->Write("multicore", LogWarning,
                                    "Unknown core %u", nCore);
            break;
    }
}

#endif // ARM_ALLOW_MULTI_CORE
