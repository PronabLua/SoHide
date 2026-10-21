#include "HideSo/HideSo.h"

HideSo::Status HideSoPublicApiCompileTest(const void* ModuleAddress, HideSo::Result* HideResult)
{
    HideSo::Request HideRequest = {ModuleAddress, HideSo::Default};
    return HideSo::Calls(HideRequest, HideResult);
}
