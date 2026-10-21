#include "SoHide/SoHide.h"

extern "C"
{
    __attribute__((visibility("default"))) SoHideStatus SoHideProofRun(uint32_t Options, SoHideResult* Result)
    {
        return SoHideHideAddress((const void*)&SoHideProofRun, Options, Result);
    }
}
