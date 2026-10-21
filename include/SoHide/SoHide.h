#pragma once

#include <stddef.h>
#include <stdint.h>

#if !defined(__aarch64__)
#error SoHide requires AArch64
#endif

#define SoHideApi __attribute__((visibility("default")))

#if defined(__cplusplus)
extern "C"
{
#endif
    typedef enum SoHideStatus
    {
        SoHideStatusSuccess = 0,
        SoHideStatusInvalidArgument = 1,
        SoHideStatusUnsupportedArchitecture = 2,
        SoHideStatusImageNotFound = 3,
        SoHideStatusMapsUnavailable = 4,
        SoHideStatusMapLimitExceeded = 5,
        SoHideStatusUnsupportedMapping = 6,
        SoHideStatusAllocationFailed = 7,
        SoHideStatusProtectionFailed = 8,
        SoHideStatusRemapFailed = 9,
        SoHideStatusLinkerNotFound = 10,
        SoHideStatusLinkerSymbolsUnavailable = 11,
        SoHideStatusLinkerLayoutUnavailable = 12,
        SoHideStatusLinkerEntryUnavailable = 13,
        SoHideStatusLinkerPatchFailed = 14,
        SoHideStatusHeaderUnavailable = 15,
        SoHideStatusVerificationFailed = 16
    } SoHideStatus;

    typedef enum SoHideOption
    {
        SoHideOptionNone = 0,
        SoHideOptionRemapFileMappings = 1U << 0U,
        SoHideOptionUnlinkLinkerList = 1U << 1U,
        SoHideOptionClearElfHeader = 1U << 2U,
        SoHideOptionVerify = 1U << 3U,
        SoHideOptionDefault = SoHideOptionRemapFileMappings | SoHideOptionUnlinkLinkerList | SoHideOptionVerify,
        SoHideOptionAggressive = SoHideOptionDefault | SoHideOptionClearElfHeader
    } SoHideOption;

    typedef struct SoHideResult
    {
        uint32_t StructSize;
        SoHideStatus Status;
        uint32_t RequestedOptions;
        uint32_t CompletedOptions;
        uintptr_t ImageBase;
        size_t MappingCount;
        size_t RemappedBytes;
        char ImagePath[512];
    } SoHideResult;

    SoHideApi SoHideStatus SoHideHideAddress(const void* ModuleAddress, uint32_t Options, SoHideResult* Result);
    SoHideApi SoHideStatus SoHideHideSelf(uint32_t Options, SoHideResult* Result);
    SoHideApi const char* SoHideStatusString(SoHideStatus Status);
#if defined(__cplusplus)
}
#endif
