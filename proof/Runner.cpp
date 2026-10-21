#include <dlfcn.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SoHide/SoHide.h"

struct LinkerSearch
{
    const char* Path;
    size_t Matches;
};

static int CountLinkerEntry(dl_phdr_info* ProgramHeaderInformation, size_t StructureSize, void* SearchContext)
{
    (void)StructureSize;
    LinkerSearch* Search = (LinkerSearch*)SearchContext;
    if (ProgramHeaderInformation->dlpi_name != nullptr && strcmp(ProgramHeaderInformation->dlpi_name, Search->Path) == 0)
    {
        ++Search->Matches;
    }
    return 0;
}

static size_t CountLinkerEntries(const char* Path)
{
    LinkerSearch Search = {Path, 0};
    dl_iterate_phdr(CountLinkerEntry, &Search);
    return Search.Matches;
}

static size_t CountMapEntries(const char* Path)
{
    FILE* MapsFile = fopen("/proc/self/maps", "re");
    if (MapsFile == nullptr)
    {
        return SIZE_MAX;
    }

    size_t Matches = 0;
    char* MapLine = nullptr;
    size_t MapLineCapacity = 0;
    while (getline(&MapLine, &MapLineCapacity, MapsFile) >= 0)
    {
        if (strstr(MapLine, Path) != nullptr)
        {
            ++Matches;
        }
    }
    free(MapLine);
    fclose(MapsFile);
    return Matches;
}

int main(int ArgumentCount, char** Arguments)
{
    if (ArgumentCount < 2 || ArgumentCount > 3)
    {
        fprintf(stderr, "usage: %s /absolute/path/libSoHideProofPayload.so [--aggressive]\n", Arguments[0]);
        return 2;
    }

    uint32_t Options = ArgumentCount == 3 && strcmp(Arguments[2], "--aggressive") == 0 ? SoHideOptionAggressive : SoHideOptionDefault;

    void* PayloadHandle = dlopen(Arguments[1], RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (PayloadHandle == nullptr)
    {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 3;
    }

    void* ProofSymbol = dlsym(PayloadHandle, "SoHideProofRun");
    if (ProofSymbol == nullptr)
    {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return 4;
    }

    size_t MapsBefore = CountMapEntries(Arguments[1]);
    size_t LinkerBefore = CountLinkerEntries(Arguments[1]);
    SoHideResult Result = {};
    SoHideStatus Status = ((SoHideStatus (*)(uint32_t, SoHideResult*))ProofSymbol)(Options, &Result);
    size_t MapsAfter = CountMapEntries(Arguments[1]);
    size_t LinkerAfter = CountLinkerEntries(Arguments[1]);

    printf("status=%d requested=0x%x completed=0x%x base=%p mappings=%zu bytes=%zu\n", (int)Status, Result.RequestedOptions, Result.CompletedOptions, (void*)Result.ImageBase, Result.MappingCount, Result.RemappedBytes);
    printf("maps=%zu->%zu linker=%zu->%zu path=%s\n", MapsBefore, MapsAfter, LinkerBefore, LinkerAfter, Result.ImagePath);

    bool ProofPassed = Status == SoHideStatusSuccess && MapsBefore != 0 && MapsAfter == 0 && LinkerBefore != 0 && LinkerAfter == 0;
    return ProofPassed ? 0 : 1;
}
