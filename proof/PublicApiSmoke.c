#include "SoHide/SoHide.h"

SoHideStatus SoHidePublicApiSmoke(const void* ModuleAddress, SoHideResult* Result)
{
    return SoHideHideAddress(ModuleAddress, SoHideOptionDefault, Result);
}
