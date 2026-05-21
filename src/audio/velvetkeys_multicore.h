//
// velvetkeys_multicore.h
//
#ifndef _velvetkeys_multicore_h
#define _velvetkeys_multicore_h

#ifdef ARM_ALLOW_MULTI_CORE

#include <circle/multicore.h>
#include <circle/types.h>

class CKernel;

class CVelvetKeysMultiCore : public CMultiCoreSupport
{
public:
    CVelvetKeysMultiCore(CKernel *pKernel);
    
    void Run(unsigned nCore) override;

private:
    CKernel *m_pKernel;
};

#endif // ARM_ALLOW_MULTI_CORE
#endif