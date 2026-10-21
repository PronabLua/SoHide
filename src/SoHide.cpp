#include "SoHide/SoHide.h"

#include <string.h>

#include "LinkerList.hpp"
#include "MemoryMaps.hpp"

namespace SoHideInternal {
    static void InitializeResult(SoHideResult* Result, uint32_t Options)
    {
        memset(Result, 0, sizeof(*Result));
        Result->StructSize = sizeof(*Result);
        Result->Status = SoHideStatusSuccess;
        Result->RequestedOptions = Options;
    }

    static void CopyResultPath(SoHideResult* Result, const char* Path)
    {
        size_t PathLength = strnlen(Path, sizeof(Result->ImagePath) - 1);
        memcpy(Result->ImagePath, Path, PathLength);
        Result->ImagePath[PathLength] = '\0';
    }
}

extern "C"
{
    SoHideStatus SoHideHideAddress(const void* ModuleAddress, uint32_t Options, SoHideResult* Result)
    {
        if (Result == nullptr)
        {
            return SoHideStatusInvalidArgument;
        }

        SoHideInternal::InitializeResult(Result, Options);
        uint32_t KnownOptions = SoHideOptionRemapFileMappings | SoHideOptionUnlinkLinkerList | SoHideOptionClearElfHeader | SoHideOptionVerify;
        if (ModuleAddress == nullptr || Options == SoHideOptionNone || (Options & ~KnownOptions) != 0)
        {
            Result->Status = SoHideStatusInvalidArgument;
            return Result->Status;
        }

        SoHideInternal::ImageIdentity Identity = {};
        Result->Status = SoHideInternal::FindImageIdentity(ModuleAddress, &Identity);
        if (Result->Status != SoHideStatusSuccess)
        {
            return Result->Status;
        }
        Result->ImageBase = Identity.Base;
        SoHideInternal::CopyResultPath(Result, Identity.Path);

        if ((Options & SoHideOptionRemapFileMappings) != 0)
        {
            Result->Status = SoHideInternal::RemapImageMappings(Identity, &Result->MappingCount, &Result->RemappedBytes);
            if (Result->Status != SoHideStatusSuccess)
            {
                return Result->Status;
            }
            Result->CompletedOptions |= SoHideOptionRemapFileMappings;
        }

        if ((Options & SoHideOptionUnlinkLinkerList) != 0)
        {
            Result->Status = SoHideInternal::UnlinkImageFromLinker(Identity);
            if (Result->Status != SoHideStatusSuccess)
            {
                return Result->Status;
            }
            Result->CompletedOptions |= SoHideOptionUnlinkLinkerList;
        }

        if ((Options & SoHideOptionClearElfHeader) != 0)
        {
            Result->Status = SoHideInternal::ClearImageHeader(Identity);
            if (Result->Status != SoHideStatusSuccess)
            {
                return Result->Status;
            }
            Result->CompletedOptions |= SoHideOptionClearElfHeader;
        }

        if ((Options & SoHideOptionVerify) != 0)
        {
            bool MapsValid = (Options & SoHideOptionRemapFileMappings) == 0 || !SoHideInternal::HasImagePath(Identity);
            bool LinkerValid = (Options & SoHideOptionUnlinkLinkerList) == 0 || !SoHideInternal::HasLinkerEntry(Identity);
            if (!MapsValid || !LinkerValid)
            {
                Result->Status = SoHideStatusVerificationFailed;
                return Result->Status;
            }
            Result->CompletedOptions |= SoHideOptionVerify;
        }

        Result->Status = SoHideStatusSuccess;
        return Result->Status;
    }

    __attribute__((noinline)) SoHideStatus SoHideHideSelf(uint32_t Options, SoHideResult* Result)
    {
        return SoHideHideAddress((const void*)&SoHideHideSelf, Options, Result);
    }

    const char* SoHideStatusString(SoHideStatus Status)
    {
        switch (Status)
        {
        case SoHideStatusSuccess:
            return "success";
        case SoHideStatusInvalidArgument:
            return "invalid argument";
        case SoHideStatusUnsupportedArchitecture:
            return "unsupported architecture";
        case SoHideStatusImageNotFound:
            return "image not found";
        case SoHideStatusMapsUnavailable:
            return "process maps unavailable";
        case SoHideStatusMapLimitExceeded:
            return "mapping limit exceeded";
        case SoHideStatusUnsupportedMapping:
            return "unsupported mapping";
        case SoHideStatusAllocationFailed:
            return "allocation failed";
        case SoHideStatusProtectionFailed:
            return "memory protection failed";
        case SoHideStatusRemapFailed:
            return "memory remap failed";
        case SoHideStatusLinkerNotFound:
            return "Android linker not found";
        case SoHideStatusLinkerSymbolsUnavailable:
            return "Android linker symbols unavailable";
        case SoHideStatusLinkerLayoutUnavailable:
            return "Android linker layout unavailable";
        case SoHideStatusLinkerEntryUnavailable:
            return "linker entry unavailable";
        case SoHideStatusLinkerPatchFailed:
            return "linker patch failed";
        case SoHideStatusHeaderUnavailable:
            return "ELF header unavailable";
        case SoHideStatusVerificationFailed:
            return "verification failed";
        }
        return "unknown status";
    }
}
