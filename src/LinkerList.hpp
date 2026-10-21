#pragma once

#include "MemoryMaps.hpp"

namespace HideSoPrivate {
    HideSo::Status UnlinkImageFromLinker(const ImageIdentity& Identity);
    bool HasLinkerEntry(const ImageIdentity& Identity);
}
