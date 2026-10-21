#pragma once

#include <stddef.h>
#include <stdint.h>

#if !defined(__aarch64__)
#error HideSo requires AArch64
#endif

namespace HideSo {
    enum Status
    {
        Success = 0,
        InvalidArgument = 1,
        ImageNotFound = 2,
        MapsUnavailable = 3,
        MapLimitExceeded = 4,
        UnsupportedMapping = 5,
        AllocationFailed = 6,
        ProtectionFailed = 7,
        RemapFailed = 8,
        LinkerNotFound = 9,
        LinkerSymbolsUnavailable = 10,
        LinkerLayoutUnavailable = 11,
        LinkerEntryUnavailable = 12,
        LinkerPatchFailed = 13,
        HeaderUnavailable = 14,
        VerificationFailed = 15
    };

    enum Option : uint32_t
    {
        None = 0,
        RemapFileMappings = 1U << 0U,
        UnlinkLinkerList = 1U << 1U,
        ClearElfHeader = 1U << 2U,
        Verify = 1U << 3U,
        Default = RemapFileMappings | UnlinkLinkerList | Verify,
        Aggressive = Default | ClearElfHeader
    };

    struct Request
    {
        const void* ModuleAddress;
        uint32_t Options;
    };

    struct Result
    {
        Status OperationStatus;
        uint32_t RequestedOptions;
        uint32_t CompletedOptions;
        uintptr_t ImageBase;
        size_t MappingCount;
        size_t RemappedBytes;
        char ImagePath[512];
    };

    Status Calls(const Request& HideRequest, Result* HideResult);
    const char* StatusText(Status OperationStatus);
}
