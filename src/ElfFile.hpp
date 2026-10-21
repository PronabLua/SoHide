#pragma once

#include <stdint.h>

#include "HideSo/HideSo.h"

namespace HideSoPrivate {
    struct LinkerSymbols
    {
        uintptr_t ListHeadStorage;
        uintptr_t ListTailStorage;
    };

    HideSo::Status FindLinkerSymbols(uintptr_t LinkerBase, const char* LinkerPath, LinkerSymbols* Symbols);
}
