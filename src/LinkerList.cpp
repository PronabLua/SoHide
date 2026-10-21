#include "LinkerList.hpp"

#include <elf.h>
#include <link.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(SoHideDiagnostics)
#include <android/log.h>
#endif

#include "ElfFile.hpp"

namespace SoHideInternal {
    static constexpr size_t MaximumObjectBytes = 1024;
    static constexpr size_t MaximumListEntries = 512;

    struct LinkerIdentity
    {
        uintptr_t Base;
        char Path[768];
        bool Found;
    };

    static void CopyText(char* Destination, size_t Capacity, const char* Source)
    {
        size_t SourceLength = Source == nullptr ? 0 : strnlen(Source, Capacity - 1);
        if (SourceLength != 0)
        {
            memcpy(Destination, Source, SourceLength);
        }
        Destination[SourceLength] = '\0';
    }

    static int FindLinkerCallback(dl_phdr_info* ProgramHeaderInformation, size_t StructureSize, void* SearchContext)
    {
        (void)StructureSize;
        LinkerIdentity* Identity = (LinkerIdentity*)SearchContext;
        const char* ImagePath = ProgramHeaderInformation->dlpi_name;
        if (ImagePath == nullptr)
        {
            return 0;
        }

        const char* FileName = strrchr(ImagePath, '/');
        FileName = FileName == nullptr ? ImagePath : FileName + 1;
        if (strcmp(FileName, "linker64") != 0)
        {
            return 0;
        }

        Identity->Base = (uintptr_t)ProgramHeaderInformation->dlpi_addr;
        CopyText(Identity->Path, sizeof(Identity->Path), ImagePath);
        Identity->Found = Identity->Base != 0 && Identity->Path[0] != '\0';
        return Identity->Found ? 1 : 0;
    }

    static bool ReadPointer(uintptr_t Address, uintptr_t* Value)
    {
        if (Value == nullptr || !IsReadableAddress(Address, sizeof(uintptr_t)))
        {
            return false;
        }
        memcpy(Value, (const void*)Address, sizeof(uintptr_t));
        return true;
    }

    static bool MatchesImageEntry(uintptr_t ObjectAddress, const ImageIdentity& Identity)
    {
        uintptr_t ProgramHeaders = 0;
        uintptr_t ImageBase = 0;
        return ReadPointer(ObjectAddress, &ProgramHeaders) && ReadPointer(ObjectAddress + sizeof(uintptr_t) * 2, &ImageBase) && ProgramHeaders == (uintptr_t)Identity.ProgramHeaders && ImageBase == Identity.Base;
    }

    static bool ValidateNextOffset(uintptr_t ListHead, uintptr_t ListTail, size_t NextOffset)
    {
        uintptr_t CurrentEntry = ListHead;
        for (size_t EntryIndex = 0; EntryIndex < MaximumListEntries; ++EntryIndex)
        {
            uintptr_t NextEntry = 0;
            if (!ReadPointer(CurrentEntry + NextOffset, &NextEntry))
            {
                return false;
            }
            if (CurrentEntry == ListTail)
            {
                return NextEntry == 0;
            }
            if (NextEntry == 0 || NextEntry == CurrentEntry || (NextEntry & (alignof(uintptr_t) - 1)) != 0)
            {
                return false;
            }
            CurrentEntry = NextEntry;
        }
        return false;
    }

    static bool FindNextOffset(uintptr_t ListHead, uintptr_t ListTail, size_t* NextOffset)
    {
        if (NextOffset == nullptr || !IsReadableAddress(ListHead, MaximumObjectBytes))
        {
            return false;
        }

        for (size_t FieldOffset = 0; FieldOffset < MaximumObjectBytes; FieldOffset += sizeof(uintptr_t))
        {
            if (ValidateNextOffset(ListHead, ListTail, FieldOffset))
            {
                *NextOffset = FieldOffset;
                return true;
            }
        }
        return false;
    }

    static bool FindTargetEntry(uintptr_t ListHead, size_t NextOffset, const ImageIdentity& Identity, uintptr_t* PreviousEntry, uintptr_t* TargetEntry, uintptr_t* NextEntry)
    {
        uintptr_t Previous = 0;
        uintptr_t Current = ListHead;
        for (size_t EntryIndex = 0; EntryIndex < MaximumListEntries && Current != 0; ++EntryIndex)
        {
            uintptr_t Following = 0;
            if (!ReadPointer(Current + NextOffset, &Following))
            {
                return false;
            }

            if (MatchesImageEntry(Current, Identity))
            {
                *PreviousEntry = Previous;
                *TargetEntry = Current;
                *NextEntry = Following;
                return true;
            }

            Previous = Current;
            Current = Following;
        }
        return false;
    }

    static bool WritePointer(uintptr_t Address, uintptr_t Value)
    {
        long PageSizeResult = sysconf(_SC_PAGESIZE);
        if (PageSizeResult <= 0 || !IsReadableAddress(Address, sizeof(uintptr_t)))
        {
            return false;
        }

        size_t PageSize = (size_t)PageSizeResult;
        uintptr_t PageAddress = Address & ~(uintptr_t)(PageSize - 1);
        int OriginalProtection = 0;
        if (!GetAddressProtection(Address, &OriginalProtection))
        {
            return false;
        }
        if (mprotect((void*)PageAddress, PageSize, PROT_READ | PROT_WRITE) != 0)
        {
            return false;
        }
        memcpy((void*)Address, &Value, sizeof(Value));
        bool Restored = mprotect((void*)PageAddress, PageSize, OriginalProtection) == 0;
        return Restored;
    }

    static SoHideStatus ResolveLinkerList(LinkerSymbols* Symbols, uintptr_t* ListHead, uintptr_t* ListTail, size_t* NextOffset)
    {
        LinkerIdentity Linker = {};
        dl_iterate_phdr(FindLinkerCallback, &Linker);
        if (!Linker.Found)
        {
            return SoHideStatusLinkerNotFound;
        }

        SoHideStatus Status = FindLinkerSymbols(Linker.Base, Linker.Path, Symbols);
        if (Status != SoHideStatusSuccess)
        {
            return Status;
        }
        if (!ReadPointer(Symbols->ListHeadStorage, ListHead) || !ReadPointer(Symbols->ListTailStorage, ListTail) || *ListHead == 0 || *ListTail == 0)
        {
            return SoHideStatusLinkerLayoutUnavailable;
        }
        if (!FindNextOffset(*ListHead, *ListTail, NextOffset))
        {
            return SoHideStatusLinkerLayoutUnavailable;
        }
        return SoHideStatusSuccess;
    }

    SoHideStatus UnlinkImageFromLinker(const ImageIdentity& Identity)
    {
        LinkerSymbols Symbols = {};
        uintptr_t ListHead = 0;
        uintptr_t ListTail = 0;
        size_t NextOffset = 0;
        SoHideStatus Status = ResolveLinkerList(&Symbols, &ListHead, &ListTail, &NextOffset);
        if (Status != SoHideStatusSuccess)
        {
            return Status;
        }

#if defined(SoHideDiagnostics)
        __android_log_print(ANDROID_LOG_INFO, "SoHide", "head-storage=%p tail-storage=%p head=%p tail=%p next-offset=0x%zx", (void*)Symbols.ListHeadStorage, (void*)Symbols.ListTailStorage, (void*)ListHead, (void*)ListTail, NextOffset);
#endif

        uintptr_t PreviousEntry = 0;
        uintptr_t TargetEntry = 0;
        uintptr_t NextEntry = 0;
        if (!FindTargetEntry(ListHead, NextOffset, Identity, &PreviousEntry, &TargetEntry, &NextEntry) || PreviousEntry == 0)
        {
            return SoHideStatusLinkerEntryUnavailable;
        }
#if defined(SoHideDiagnostics)
        __android_log_print(ANDROID_LOG_INFO, "SoHide", "previous=%p target=%p next=%p base=%p phdr=%p", (void*)PreviousEntry, (void*)TargetEntry, (void*)NextEntry, (void*)Identity.Base, Identity.ProgramHeaders);
#endif
        if (!WritePointer(PreviousEntry + NextOffset, NextEntry))
        {
            return SoHideStatusLinkerPatchFailed;
        }
#if defined(SoHideDiagnostics)
        uintptr_t PatchedEntry = 0;
        ReadPointer(PreviousEntry + NextOffset, &PatchedEntry);
        uintptr_t RemainingPrevious = 0;
        uintptr_t RemainingTarget = 0;
        uintptr_t RemainingNext = 0;
        bool StillReachable = FindTargetEntry(ListHead, NextOffset, Identity, &RemainingPrevious, &RemainingTarget, &RemainingNext);
        __android_log_print(ANDROID_LOG_INFO, "SoHide", "patched-next=%p reachable=%d remaining-previous=%p remaining-target=%p remaining-next=%p", (void*)PatchedEntry, StillReachable ? 1 : 0, (void*)RemainingPrevious, (void*)RemainingTarget, (void*)RemainingNext);
#endif
        if (TargetEntry == ListTail && !WritePointer(Symbols.ListTailStorage, PreviousEntry))
        {
            return SoHideStatusLinkerPatchFailed;
        }
        return SoHideStatusSuccess;
    }

    struct EntrySearch
    {
        const ImageIdentity* Identity;
        bool Found;
    };

    static int FindEntryCallback(dl_phdr_info* ProgramHeaderInformation, size_t StructureSize, void* SearchContext)
    {
        (void)StructureSize;
        EntrySearch* Search = (EntrySearch*)SearchContext;
        if ((uintptr_t)ProgramHeaderInformation->dlpi_addr == Search->Identity->Base)
        {
#if defined(SoHideDiagnostics)
            __android_log_print(ANDROID_LOG_INFO, "SoHide", "verification-entry=%p phdr=%p path=%s", (void*)ProgramHeaderInformation->dlpi_addr, ProgramHeaderInformation->dlpi_phdr, ProgramHeaderInformation->dlpi_name == nullptr ? "" : ProgramHeaderInformation->dlpi_name);
#endif
            Search->Found = true;
            return 1;
        }
        return 0;
    }

    bool HasLinkerEntry(const ImageIdentity& Identity)
    {
        EntrySearch Search = {&Identity, false};
        dl_iterate_phdr(FindEntryCallback, &Search);
        return Search.Found;
    }
}
