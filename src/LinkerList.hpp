#pragma once

#include "MemoryMaps.hpp"

namespace SoHideInternal {
    SoHideStatus UnlinkImageFromLinker(const ImageIdentity& Identity);
    bool HasLinkerEntry(const ImageIdentity& Identity);
}
