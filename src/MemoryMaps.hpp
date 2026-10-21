#pragma once

#include <stddef.h>
#include <stdint.h>

#include "HideSo/HideSo.h"

namespace HideSoPrivate {
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

    HideSo::Status ReadMemoryMaps(MemoryMap* Mappings, size_t Capacity, size_t* Count);
    HideSo::Status FindImageIdentity(const void* ModuleAddress, ImageIdentity* Identity);
    HideSo::Status CollectImageMappings(const ImageIdentity& Identity, MemoryMap* Mappings, size_t Capacity, size_t* Count);
    bool IsReadableAddress(uintptr_t Address, size_t Length);
    bool GetAddressProtection(uintptr_t Address, int* Protection);
    bool HasImagePath(const ImageIdentity& Identity);
    HideSo::Status RemapImageMappings(const ImageIdentity& Identity, size_t* MappingCount, size_t* RemappedBytes);
    HideSo::Status ClearImageHeader(const ImageIdentity& Identity);
}
