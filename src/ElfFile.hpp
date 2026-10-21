#pragma once

#include <stdint.h>

#include "SoHide/SoHide.h"

namespace SoHideInternal {
    struct LinkerSymbols
    {
        uintptr_t ListHeadStorage;
        uintptr_t ListTailStorage;
    };

    SoHideStatus FindLinkerSymbols(uintptr_t LinkerBase, const char* LinkerPath, LinkerSymbols* Symbols);
}
