//
// microdx21_multicore.h
//
#ifndef _MICRODX21_MULTICORE_H
#define _MICRODX21_MULTICORE_H

#ifdef ARM_ALLOW_MULTI_CORE

#include <circle/multicore.h>
#include <circle/types.h>

class CKernel;

class CMicroDX21MultiCore : public CMultiCoreSupport
{
public:
    CMicroDX21MultiCore(CKernel *pKernel);
    
    void Run(unsigned nCore) override;

private:
    CKernel *m_pKernel;
};

#endif // ARM_ALLOW_MULTI_CORE
#endif