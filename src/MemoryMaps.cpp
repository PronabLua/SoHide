#include "MemoryMaps.hpp"

#include <elf.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace HideSoPrivate {
    static constexpr size_t MaximumMapCount = 2048;
    static constexpr size_t MaximumImageMapCount = 32;

    static void CopyText(char* Destination, size_t Capacity, const char* Source)
    {
        if (Capacity == 0)
        {
            return;
        }

        size_t SourceLength = Source == nullptr ? 0 : strnlen(Source, Capacity - 1);
        if (SourceLength != 0)
        {
            memcpy(Destination, Source, SourceLength);
        }
        Destination[SourceLength] = '\0';
    }

    static void TrimPath(char* Path)
    {
        size_t PathLength = strlen(Path);
        while (PathLength != 0 && (Path[PathLength - 1] == '\n' || Path[PathLength - 1] == '\r' || Path[PathLength - 1] == ' '))
        {
            Path[--PathLength] = '\0';
        }

        size_t LeadingSpaces = 0;
        while (Path[LeadingSpaces] == ' ')
        {
            ++LeadingSpaces;
        }
        if (LeadingSpaces != 0)
        {
            memmove(Path, Path + LeadingSpaces, strlen(Path + LeadingSpaces) + 1);
        }
    }

    HideSo::Status ReadMemoryMaps(MemoryMap* Mappings, size_t Capacity, size_t* Count)
    {
        if (Mappings == nullptr || Capacity == 0 || Count == nullptr)
        {
            return HideSo::InvalidArgument;
        }

        *Count = 0;
        FILE* MapsFile = fopen("/proc/self/maps", "re");
        if (MapsFile == nullptr)
        {
            return HideSo::MapsUnavailable;
        }

        char* MapLine = nullptr;
        size_t MapLineCapacity = 0;
        while (getline(&MapLine, &MapLineCapacity, MapsFile) >= 0)
        {
            if (*Count == Capacity)
            {
                free(MapLine);
                fclose(MapsFile);
                return HideSo::MapLimitExceeded;
            }

            unsigned long long Start = 0;
            unsigned long long End = 0;
            unsigned long long Offset = 0;
            unsigned long long Inode = 0;
            unsigned int DeviceMajor = 0;
            unsigned int DeviceMinor = 0;
            char Permissions[5] = {};
            int PathOffset = 0;
            int FieldCount = sscanf(MapLine, "%llx-%llx %4s %llx %x:%x %llu %n", &Start, &End, Permissions, &Offset, &DeviceMajor, &DeviceMinor, &Inode, &PathOffset);
            if (FieldCount != 7 || Start >= End)
            {
                continue;
            }

            MemoryMap& Mapping = Mappings[(*Count)++];
            memset(&Mapping, 0, sizeof(Mapping));
            Mapping.Start = (uintptr_t)Start;
            Mapping.End = (uintptr_t)End;
            Mapping.Offset = (uint64_t)Offset;
            Mapping.Inode = (uint64_t)Inode;
            Mapping.DeviceMajor = DeviceMajor;
            Mapping.DeviceMinor = DeviceMinor;
            Mapping.Protection = (Permissions[0] == 'r' ? PROT_READ : 0) | (Permissions[1] == 'w' ? PROT_WRITE : 0) | (Permissions[2] == 'x' ? PROT_EXEC : 0);
            Mapping.PrivateMapping = Permissions[3] == 'p';
            CopyText(Mapping.Path, sizeof(Mapping.Path), MapLine + PathOffset);
            TrimPath(Mapping.Path);
        }

        free(MapLine);
        fclose(MapsFile);
        return HideSo::Success;
    }

    struct ImageSearch
    {
        uintptr_t Address;
        ImageIdentity* Identity;
        bool Found;
    };

    static int FindImageCallback(dl_phdr_info* ProgramHeaderInformation, size_t StructureSize, void* SearchContext)
    {
        (void)StructureSize;
        ImageSearch* Search = (ImageSearch*)SearchContext;
        for (size_t HeaderIndex = 0; HeaderIndex < ProgramHeaderInformation->dlpi_phnum; ++HeaderIndex)
        {
            const ElfW(Phdr)& ProgramHeader = ProgramHeaderInformation->dlpi_phdr[HeaderIndex];
            if (ProgramHeader.p_type != PT_LOAD)
            {
                continue;
            }

            uintptr_t SegmentStart = (uintptr_t)ProgramHeaderInformation->dlpi_addr + ProgramHeader.p_vaddr;
            uintptr_t SegmentEnd = SegmentStart + ProgramHeader.p_memsz;
            if (Search->Address < SegmentStart || Search->Address >= SegmentEnd)
            {
                continue;
            }

            Search->Identity->Base = (uintptr_t)ProgramHeaderInformation->dlpi_addr;
            Search->Identity->ProgramHeaders = ProgramHeaderInformation->dlpi_phdr;
            Search->Identity->ProgramHeaderCount = ProgramHeaderInformation->dlpi_phnum;
            CopyText(Search->Identity->Path, sizeof(Search->Identity->Path), ProgramHeaderInformation->dlpi_name);
            Search->Found = true;
            return 1;
        }
        return 0;
    }

    HideSo::Status FindImageIdentity(const void* ModuleAddress, ImageIdentity* Identity)
    {
        if (ModuleAddress == nullptr || Identity == nullptr)
        {
            return HideSo::InvalidArgument;
        }

        memset(Identity, 0, sizeof(*Identity));
        ImageSearch Search = {(uintptr_t)ModuleAddress, Identity, false};
        dl_iterate_phdr(FindImageCallback, &Search);
        if (!Search.Found || Identity->Path[0] == '\0')
        {
            return HideSo::ImageNotFound;
        }

        MemoryMap Mappings[MaximumMapCount] = {};
        size_t MappingCount = 0;
        HideSo::Status Status = ReadMemoryMaps(Mappings, MaximumMapCount, &MappingCount);
        if (Status != HideSo::Success)
        {
            return Status;
        }

        uintptr_t Address = (uintptr_t)ModuleAddress;
        for (size_t MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
        {
            const MemoryMap& Mapping = Mappings[MappingIndex];
            if (Address >= Mapping.Start && Address < Mapping.End)
            {
                Identity->Inode = Mapping.Inode;
                Identity->DeviceMajor = Mapping.DeviceMajor;
                Identity->DeviceMinor = Mapping.DeviceMinor;
                if (Mapping.Path[0] != '\0')
                {
                    CopyText(Identity->Path, sizeof(Identity->Path), Mapping.Path);
                }
                return HideSo::Success;
            }
        }

        return HideSo::ImageNotFound;
    }

    static bool IsSameImage(const ImageIdentity& Identity, const MemoryMap& Mapping)
    {
        if (Identity.Inode != 0 && Mapping.Inode == Identity.Inode && Mapping.DeviceMajor == Identity.DeviceMajor && Mapping.DeviceMinor == Identity.DeviceMinor)
        {
            return true;
        }
        return Identity.Path[0] != '\0' && strcmp(Identity.Path, Mapping.Path) == 0;
    }

    static bool IsWithinImageLoadRange(const ImageIdentity& Identity, const MemoryMap& Mapping)
    {
        const ElfW(Phdr)* ProgramHeaders = (const ElfW(Phdr)*)Identity.ProgramHeaders;
        for (size_t HeaderIndex = 0; HeaderIndex < Identity.ProgramHeaderCount; ++HeaderIndex)
        {
            const ElfW(Phdr)& ProgramHeader = ProgramHeaders[HeaderIndex];
            if (ProgramHeader.p_type != PT_LOAD)
            {
                continue;
            }

            uintptr_t SegmentStart = Identity.Base + ProgramHeader.p_vaddr;
            uintptr_t SegmentEnd = SegmentStart + ProgramHeader.p_memsz;
            if (Mapping.Start < SegmentEnd && Mapping.End > SegmentStart)
            {
                return true;
            }
        }
        return false;
    }

    HideSo::Status CollectImageMappings(const ImageIdentity& Identity, MemoryMap* Mappings, size_t Capacity, size_t* Count)
    {
        if (Mappings == nullptr || Capacity == 0 || Count == nullptr)
        {
            return HideSo::InvalidArgument;
        }

        MemoryMap ProcessMappings[MaximumMapCount] = {};
        size_t ProcessMappingCount = 0;
        HideSo::Status Status = ReadMemoryMaps(ProcessMappings, MaximumMapCount, &ProcessMappingCount);
        if (Status != HideSo::Success)
        {
            return Status;
        }

        *Count = 0;
        for (size_t MappingIndex = 0; MappingIndex < ProcessMappingCount; ++MappingIndex)
        {
            if (!IsSameImage(Identity, ProcessMappings[MappingIndex]) || !IsWithinImageLoadRange(Identity, ProcessMappings[MappingIndex]))
            {
                continue;
            }
            if (*Count == Capacity)
            {
                return HideSo::MapLimitExceeded;
            }
            Mappings[(*Count)++] = ProcessMappings[MappingIndex];
        }

        return *Count == 0 ? HideSo::ImageNotFound : HideSo::Success;
    }

    bool IsReadableAddress(uintptr_t Address, size_t Length)
    {
        if (Address == 0 || Length == 0 || Address + Length < Address)
        {
            return false;
        }

        MemoryMap Mappings[MaximumMapCount] = {};
        size_t MappingCount = 0;
        if (ReadMemoryMaps(Mappings, MaximumMapCount, &MappingCount) != HideSo::Success)
        {
            return false;
        }

        for (size_t MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
        {
            const MemoryMap& Mapping = Mappings[MappingIndex];
            if ((Mapping.Protection & PROT_READ) != 0 && Address >= Mapping.Start && Address + Length <= Mapping.End)
            {
                return true;
            }
        }
        return false;
    }

    bool GetAddressProtection(uintptr_t Address, int* Protection)
    {
        if (Protection == nullptr)
        {
            return false;
        }

        MemoryMap Mappings[MaximumMapCount] = {};
        size_t MappingCount = 0;
        if (ReadMemoryMaps(Mappings, MaximumMapCount, &MappingCount) != HideSo::Success)
        {
            return false;
        }

        for (size_t MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
        {
            if (Address >= Mappings[MappingIndex].Start && Address < Mappings[MappingIndex].End)
            {
                *Protection = Mappings[MappingIndex].Protection;
                return true;
            }
        }
        return false;
    }

    bool HasImagePath(const ImageIdentity& Identity)
    {
        MemoryMap Mappings[MaximumMapCount] = {};
        size_t MappingCount = 0;
        if (ReadMemoryMaps(Mappings, MaximumMapCount, &MappingCount) != HideSo::Success)
        {
            return true;
        }

        for (size_t MappingIndex = 0; MappingIndex < MappingCount; ++MappingIndex)
        {
            if (IsSameImage(Identity, Mappings[MappingIndex]) && IsWithinImageLoadRange(Identity, Mappings[MappingIndex]))
            {
                return true;
            }
        }
        return false;
    }

    static HideSo::Status RemapMapping(const MemoryMap& Mapping)
    {
        if (!Mapping.PrivateMapping || (Mapping.Protection & PROT_READ) == 0)
        {
            return HideSo::UnsupportedMapping;
        }

        size_t MappingLength = Mapping.End - Mapping.Start;
        void* ShadowMapping = mmap(nullptr, MappingLength, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ShadowMapping == MAP_FAILED)
        {
            return HideSo::AllocationFailed;
        }

        memcpy(ShadowMapping, (const void*)Mapping.Start, MappingLength);
        if (mprotect(ShadowMapping, MappingLength, Mapping.Protection) != 0)
        {
            munmap(ShadowMapping, MappingLength);
            return HideSo::ProtectionFailed;
        }

        void* RemappedAddress = (void*)syscall(__NR_mremap, ShadowMapping, MappingLength, MappingLength, MREMAP_MAYMOVE | MREMAP_FIXED, (void*)Mapping.Start);
        if (RemappedAddress != (void*)Mapping.Start)
        {
            if (RemappedAddress != MAP_FAILED)
            {
                munmap(RemappedAddress, MappingLength);
            }
            else
            {
                munmap(ShadowMapping, MappingLength);
            }
            return HideSo::RemapFailed;
        }

        if ((Mapping.Protection & PROT_EXEC) != 0)
        {
            __builtin___clear_cache((char*)Mapping.Start, (char*)Mapping.End);
        }
        return HideSo::Success;
    }

    HideSo::Status RemapImageMappings(const ImageIdentity& Identity, size_t* MappingCount, size_t* RemappedBytes)
    {
        if (MappingCount == nullptr || RemappedBytes == nullptr)
        {
            return HideSo::InvalidArgument;
        }

        MemoryMap ImageMappings[MaximumImageMapCount] = {};
        size_t ImageMappingCount = 0;
        HideSo::Status Status = CollectImageMappings(Identity, ImageMappings, MaximumImageMapCount, &ImageMappingCount);
        if (Status != HideSo::Success)
        {
            return Status;
        }

        *MappingCount = 0;
        *RemappedBytes = 0;
        for (size_t MappingIndex = 0; MappingIndex < ImageMappingCount; ++MappingIndex)
        {
            Status = RemapMapping(ImageMappings[MappingIndex]);
            if (Status != HideSo::Success)
            {
                return Status;
            }
            ++*MappingCount;
            *RemappedBytes += ImageMappings[MappingIndex].End - ImageMappings[MappingIndex].Start;
        }
        return HideSo::Success;
    }

    HideSo::Status ClearImageHeader(const ImageIdentity& Identity)
    {
        long PageSizeResult = sysconf(_SC_PAGESIZE);
        if (PageSizeResult <= 0 || Identity.Base == 0 || !IsReadableAddress(Identity.Base, sizeof(Elf64_Ehdr)))
        {
            return HideSo::HeaderUnavailable;
        }

        size_t PageSize = (size_t)PageSizeResult;
        uintptr_t HeaderPage = Identity.Base & ~(uintptr_t)(PageSize - 1);
        int OriginalProtection = 0;
        if (!GetAddressProtection(Identity.Base, &OriginalProtection))
        {
            return HideSo::HeaderUnavailable;
        }
        if (mprotect((void*)HeaderPage, PageSize, PROT_READ | PROT_WRITE) != 0)
        {
            return HideSo::ProtectionFailed;
        }

        volatile uint8_t* HeaderBytes = (volatile uint8_t*)Identity.Base;
        for (size_t ByteIndex = 0; ByteIndex < SELFMAG; ++ByteIndex)
        {
            HeaderBytes[ByteIndex] = 0;
        }

        if (mprotect((void*)HeaderPage, PageSize, OriginalProtection) != 0)
        {
            return HideSo::ProtectionFailed;
        }
        return HideSo::Success;
    }
}
