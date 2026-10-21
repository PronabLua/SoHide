#include "MemoryMaps.hpp"
#include "HideSo/xorstr.hpp"

#include <elf.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace HideSoPrivate {
    MemoryMapList::~MemoryMapList()
    {
        free(Mappings);
    }

    bool MemoryMapList::Append(const MemoryMap& Mapping)
    {
        if (Count == Capacity)
        {
            size_t NewCapacity = Capacity == 0 ? 8 : Capacity * 2;
            if (NewCapacity < Capacity || NewCapacity > SIZE_MAX / sizeof(MemoryMap))
            {
                return false;
            }
            MemoryMap* ResizedMappings = (MemoryMap*)realloc(Mappings, NewCapacity * sizeof(MemoryMap));
            if (ResizedMappings == nullptr)
            {
                return false;
            }
            Mappings = ResizedMappings;
            Capacity = NewCapacity;
        }
        Mappings[Count++] = Mapping;
        return true;
    }

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

    template<typename Visitor>
    static HideSo::Status VisitMemoryMaps(Visitor&& Visit)
    {
        FILE* MapsFile = fopen(xorstr_("/proc/self/maps"), xorstr_("re"));
        if (MapsFile == nullptr)
        {
            return HideSo::MapsUnavailable;
        }

        char* MapLine = nullptr;
        size_t MapLineCapacity = 0;
        HideSo::Status Status = HideSo::Success;
        for (;;)
        {
            if (getline(&MapLine, &MapLineCapacity, MapsFile) < 0)
            {
                if (ferror(MapsFile) || !feof(MapsFile))
                {
                    Status = HideSo::MapsUnavailable;
                }
                break;
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

            MemoryMap Mapping = {};
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
            if (!Visit(Mapping))
            {
                break;
            }
        }

        free(MapLine);
        fclose(MapsFile);
        return Status;
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

        uintptr_t Address = (uintptr_t)ModuleAddress;
        bool Found = false;
        HideSo::Status Status = VisitMemoryMaps([&](const MemoryMap& Mapping)
        {
            if (Address >= Mapping.Start && Address < Mapping.End)
            {
                Identity->Inode = Mapping.Inode;
                Identity->DeviceMajor = Mapping.DeviceMajor;
                Identity->DeviceMinor = Mapping.DeviceMinor;
                if (Mapping.Path[0] != '\0')
                {
                    CopyText(Identity->Path, sizeof(Identity->Path), Mapping.Path);
                }
                Found = true;
                return false;
            }
            return true;
        });
        if (Status != HideSo::Success)
        {
            return Status;
        }
        return Found ? HideSo::Success : HideSo::ImageNotFound;
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

    HideSo::Status CollectImageMappings(const ImageIdentity& Identity, MemoryMapList* Mappings)
    {
        if (Mappings == nullptr)
        {
            return HideSo::InvalidArgument;
        }

        Mappings->Count = 0;
        bool AllocationFailed = false;
        HideSo::Status Status = VisitMemoryMaps([&](const MemoryMap& Mapping)
        {
            if (!IsSameImage(Identity, Mapping) || !IsWithinImageLoadRange(Identity, Mapping))
            {
                return true;
            }
            if (!Mappings->Append(Mapping))
            {
                AllocationFailed = true;
                return false;
            }
            return true;
        });
        if (Status != HideSo::Success)
        {
            return Status;
        }
        if (AllocationFailed)
        {
            return HideSo::AllocationFailed;
        }

        return Mappings->Count == 0 ? HideSo::ImageNotFound : HideSo::Success;
    }

    bool IsReadableAddress(uintptr_t Address, size_t Length)
    {
        if (Address == 0 || Length == 0 || Address + Length < Address)
        {
            return false;
        }

        bool Found = false;
        HideSo::Status Status = VisitMemoryMaps([&](const MemoryMap& Mapping)
        {
            if ((Mapping.Protection & PROT_READ) != 0 && Address >= Mapping.Start && Address + Length <= Mapping.End)
            {
                Found = true;
                return false;
            }
            return true;
        });
        return Status == HideSo::Success && Found;
    }

    bool GetAddressProtection(uintptr_t Address, int* Protection)
    {
        if (Protection == nullptr)
        {
            return false;
        }

        bool Found = false;
        HideSo::Status Status = VisitMemoryMaps([&](const MemoryMap& Mapping)
        {
            if (Address >= Mapping.Start && Address < Mapping.End)
            {
                *Protection = Mapping.Protection;
                Found = true;
                return false;
            }
            return true;
        });
        return Status == HideSo::Success && Found;
    }

    bool HasImagePath(const ImageIdentity& Identity)
    {
        bool Found = false;
        HideSo::Status Status = VisitMemoryMaps([&](const MemoryMap& Mapping)
        {
            if (IsSameImage(Identity, Mapping) && IsWithinImageLoadRange(Identity, Mapping))
            {
                Found = true;
                return false;
            }
            return true;
        });
        return Status != HideSo::Success || Found;
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

        *MappingCount = 0;
        *RemappedBytes = 0;
        MemoryMapList ImageMappings;
        HideSo::Status Status = CollectImageMappings(Identity, &ImageMappings);
        if (Status != HideSo::Success)
        {
            return Status;
        }

        for (size_t MappingIndex = 0; MappingIndex < ImageMappings.Count; ++MappingIndex)
        {
            const MemoryMap& Mapping = ImageMappings.Mappings[MappingIndex];
            if (!Mapping.PrivateMapping || (Mapping.Protection & PROT_READ) == 0)
            {
                return HideSo::UnsupportedMapping;
            }
        }
        for (size_t MappingIndex = 0; MappingIndex < ImageMappings.Count; ++MappingIndex)
        {
            Status = RemapMapping(ImageMappings.Mappings[MappingIndex]);
            if (Status != HideSo::Success)
            {
                return Status;
            }
            ++*MappingCount;
            *RemappedBytes += ImageMappings.Mappings[MappingIndex].End - ImageMappings.Mappings[MappingIndex].Start;
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
