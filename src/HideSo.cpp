#include "HideSo/HideSo.h"

#include <string.h>

#include "LinkerList.hpp"
#include "MemoryMaps.hpp"
#include "HideSo/OllvmProtect.h"

namespace HideSoPrivate {
    static void InitializeResult(HideSo::Result* HideResult, uint32_t Options)
    {
        memset(HideResult, 0, sizeof(*HideResult));
        HideResult->OperationStatus = HideSo::Success;
        HideResult->RequestedOptions = Options;
    }

    static void CopyResultPath(HideSo::Result* HideResult, const char* ImagePath)
    {
        size_t PathLength = strnlen(ImagePath, sizeof(HideResult->ImagePath) - 1);
        memcpy(HideResult->ImagePath, ImagePath, PathLength);
        HideResult->ImagePath[PathLength] = '\0';
    }
}

HideSo::Status HideSo::Calls(const Request& HideRequest, Result* HideResult)
{
    Result LocalResult = {};
    Result* OperationResult = HideResult == nullptr ? &LocalResult : HideResult;
    HideSoPrivate::InitializeResult(OperationResult, HideRequest.Options);

    uint32_t KnownOptions = RemapFileMappings | UnlinkLinkerList | ClearElfHeader | Verify;
    if (HideRequest.ModuleAddress == nullptr || HideRequest.Options == None || (HideRequest.Options & ~KnownOptions) != 0)
    {
        OperationResult->OperationStatus = InvalidArgument;
        return OperationResult->OperationStatus;
    }

    HideSoPrivate::ImageIdentity Identity = {};
    OperationResult->OperationStatus = HideSoPrivate::FindImageIdentity(HideRequest.ModuleAddress, &Identity);
    if (OperationResult->OperationStatus != Success)
    {
        return OperationResult->OperationStatus;
    }
    OperationResult->ImageBase = Identity.Base;
    HideSoPrivate::CopyResultPath(OperationResult, Identity.Path);

    if ((HideRequest.Options & RemapFileMappings) != 0)
    {
        OperationResult->OperationStatus = HideSoPrivate::RemapImageMappings(Identity, &OperationResult->MappingCount, &OperationResult->RemappedBytes);
        if (OperationResult->OperationStatus != Success)
        {
            return OperationResult->OperationStatus;
        }
        OperationResult->CompletedOptions |= RemapFileMappings;
    }

    if ((HideRequest.Options & UnlinkLinkerList) != 0)
    {
        OperationResult->OperationStatus = HideSoPrivate::UnlinkImageFromLinker(Identity);
        if (OperationResult->OperationStatus != Success)
        {
            return OperationResult->OperationStatus;
        }
        OperationResult->CompletedOptions |= UnlinkLinkerList;
    }

    if ((HideRequest.Options & ClearElfHeader) != 0)
    {
        OperationResult->OperationStatus = HideSoPrivate::ClearImageHeader(Identity);
        if (OperationResult->OperationStatus != Success)
        {
            return OperationResult->OperationStatus;
        }
        OperationResult->CompletedOptions |= ClearElfHeader;
    }

    if ((HideRequest.Options & Verify) != 0)
    {
        bool MapsValid = (HideRequest.Options & RemapFileMappings) == 0 || !HideSoPrivate::HasImagePath(Identity);
        bool LinkerValid = (HideRequest.Options & UnlinkLinkerList) == 0 || !HideSoPrivate::HasLinkerEntry(Identity);
        if (!MapsValid || !LinkerValid)
        {
            OperationResult->OperationStatus = VerificationFailed;
            return OperationResult->OperationStatus;
        }
        OperationResult->CompletedOptions |= Verify;
    }

    OperationResult->OperationStatus = Success;
    return OperationResult->OperationStatus;
}

const char* HideSo::StatusText(Status OperationStatus)
{
    switch (OperationStatus)
    {
    case Success:
        return "success";
    case InvalidArgument:
        return "invalid argument";
    case ImageNotFound:
        return "image not found";
    case MapsUnavailable:
        return "process maps unavailable";
    case MapLimitExceeded:
        return "mapping limit exceeded";
    case UnsupportedMapping:
        return "unsupported mapping";
    case AllocationFailed:
        return "allocation failed";
    case ProtectionFailed:
        return "memory protection failed";
    case RemapFailed:
        return "memory remap failed";
    case LinkerNotFound:
        return "Android linker not found";
    case LinkerSymbolsUnavailable:
        return "Android linker symbols unavailable";
    case LinkerLayoutUnavailable:
        return "Android linker layout unavailable";
    case LinkerEntryUnavailable:
        return "linker entry unavailable";
    case LinkerPatchFailed:
        return "linker patch failed";
    case HeaderUnavailable:
        return "ELF header unavailable";
    case VerificationFailed:
        return "verification failed";
    }
    return "unknown status";
}

#include "HideSo/OllvmProtectEnd.h"
