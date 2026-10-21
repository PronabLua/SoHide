#pragma once

#include <stddef.h>
#include <stdint.h>

#include "SoHide/SoHide.h"

namespace SoHideInternal {
    struct MemoryMap
    {
        uintptr_t Start;
        uintptr_t End;
        uint64_t Offset;
        uint64_t Inode;
        uint32_t DeviceMajor;
        uint32_t DeviceMinor;
        int Protection;
        bool PrivateMapping;
        char Path[768];
    };

    struct ImageIdentity
    {
        uintptr_t Base;
        const void* ProgramHeaders;
        size_t ProgramHeaderCount;
        uint64_t Inode;
        uint32_t DeviceMajor;
        uint32_t DeviceMinor;
        char Path[768];
    };

    SoHideStatus ReadMemoryMaps(MemoryMap* Mappings, size_t Capacity, size_t* Count);
    SoHideStatus FindImageIdentity(const void* ModuleAddress, ImageIdentity* Identity);
    SoHideStatus CollectImageMappings(const ImageIdentity& Identity, MemoryMap* Mappings, size_t Capacity, size_t* Count);
    bool IsReadableAddress(uintptr_t Address, size_t Length);
    bool GetAddressProtection(uintptr_t Address, int* Protection);
    bool HasImagePath(const ImageIdentity& Identity);
    SoHideStatus RemapImageMappings(const ImageIdentity& Identity, size_t* MappingCount, size_t* RemappedBytes);
    SoHideStatus ClearImageHeader(const ImageIdentity& Identity);
}
