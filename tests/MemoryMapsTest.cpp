#include "../src/MemoryMaps.hpp"

#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

static size_t CountProcessMappings()
{
    FILE* MapsFile = fopen("/proc/self/maps", "re");
    assert(MapsFile != nullptr);
    char* MapLine = nullptr;
    size_t LineCapacity = 0;
    size_t MappingCount = 0;
    while (getline(&MapLine, &LineCapacity, MapsFile) >= 0)
    {
        ++MappingCount;
    }
    assert(feof(MapsFile) && !ferror(MapsFile));
    free(MapLine);
    fclose(MapsFile);
    return MappingCount;
}
static void TestFragmentedImage(size_t PageSize)
{
    FILE* FixtureFile = tmpfile();
    assert(FixtureFile != nullptr);
    int FileDescriptor = fileno(FixtureFile);
    size_t ImagePageCount = 96;
    size_t ImageLength = PageSize * ImagePageCount;
    assert(ftruncate(FileDescriptor, (off_t)ImageLength) == 0);
    void* Image = mmap(nullptr, ImageLength, PROT_READ | PROT_WRITE, MAP_PRIVATE, FileDescriptor, 0);
    assert(Image != MAP_FAILED);
    for (size_t PageIndex = 0; PageIndex < ImagePageCount; ++PageIndex)
    {
        *((uint8_t*)Image + PageIndex * PageSize) = (uint8_t)(PageIndex + 1);
        if (PageIndex % 2 != 0)
        {
            assert(mprotect((char*)Image + PageIndex * PageSize, PageSize, PROT_READ) == 0);
        }
    }

    struct stat FileIdentity = {};
    assert(fstat(FileDescriptor, &FileIdentity) == 0);
    Elf64_Phdr ProgramHeader = {};
    ProgramHeader.p_type = PT_LOAD;
    ProgramHeader.p_memsz = ImageLength;
    ProgramHeader.p_filesz = ImageLength;
    HideSoPrivate::ImageIdentity Identity = {};
    Identity.Base = (uintptr_t)Image;
    Identity.ProgramHeaders = &ProgramHeader;
    Identity.ProgramHeaderCount = 1;
    Identity.Inode = FileIdentity.st_ino;
    Identity.DeviceMajor = major(FileIdentity.st_dev);
    Identity.DeviceMinor = minor(FileIdentity.st_dev);
    HideSoPrivate::MemoryMapList Mappings;
    assert(HideSoPrivate::CollectImageMappings(Identity, &Mappings) == HideSo::Success);
    printf("Fragmented image mappings: %zu\n", Mappings.Count);
    assert(Mappings.Count == ImagePageCount);

    void* LastPage = (char*)Image + (ImagePageCount - 1) * PageSize;
    assert(mmap(LastPage, PageSize, PROT_READ, MAP_SHARED | MAP_FIXED, FileDescriptor, (off_t)(ImageLength - PageSize)) == LastPage);
    size_t RemappedCount = SIZE_MAX;
    size_t RemappedBytes = SIZE_MAX;
    assert(HideSoPrivate::RemapImageMappings(Identity, &RemappedCount, &RemappedBytes) == HideSo::UnsupportedMapping);
    assert(RemappedCount == 0 && RemappedBytes == 0);
    assert(HideSoPrivate::CollectImageMappings(Identity, &Mappings) == HideSo::Success);
    assert(Mappings.Count == ImagePageCount);

    assert(mmap(LastPage, PageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, FileDescriptor, (off_t)(ImageLength - PageSize)) == LastPage);
    *(uint8_t*)LastPage = (uint8_t)ImagePageCount;
    assert(mprotect(LastPage, PageSize, PROT_READ) == 0);
    assert(HideSoPrivate::RemapImageMappings(Identity, &RemappedCount, &RemappedBytes) == HideSo::Success);
    assert(RemappedCount == ImagePageCount && RemappedBytes == ImageLength);
    assert(!HideSoPrivate::HasImagePath(Identity));
    for (size_t PageIndex = 0; PageIndex < ImagePageCount; ++PageIndex)
    {
        assert(*((const uint8_t*)Image + PageIndex * PageSize) == (uint8_t)(PageIndex + 1));
    }
    int Protection = 0;
    assert(HideSoPrivate::GetAddressProtection((uintptr_t)Image, &Protection));
    assert(Protection == (PROT_READ | PROT_WRITE));
    assert(HideSoPrivate::GetAddressProtection((uintptr_t)LastPage, &Protection));
    assert(Protection == PROT_READ);
    assert(munmap(Image, ImageLength) == 0);
    assert(fclose(FixtureFile) == 0);
}

int main()
{
    long PageSizeResult = sysconf(_SC_PAGESIZE);
    assert(PageSizeResult > 0);
    size_t PageSize = (size_t)PageSizeResult;
    size_t ReservationSize = PageSize * 8192;
    void* Reservation = mmap(nullptr, ReservationSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(Reservation != MAP_FAILED);
    for (size_t PageIndex = 0; PageIndex < 8192; PageIndex += 2)
    {
        assert(mprotect((char*)Reservation + PageIndex * PageSize, PageSize, PROT_READ) == 0);
    }

    size_t ProcessMappingCount = CountProcessMappings();
    printf("Process mappings: %zu\n", ProcessMappingCount);
    fflush(stdout);
    assert(ProcessMappingCount > 2048);
    assert(HideSoPrivate::IsReadableAddress((uintptr_t)Reservation, PageSize));
    assert(HideSoPrivate::IsReadableAddress((uintptr_t)Reservation + PageSize * 8190, PageSize));
    assert(!HideSoPrivate::IsReadableAddress((uintptr_t)Reservation + PageSize, 1));
    assert(!HideSoPrivate::IsReadableAddress((uintptr_t)Reservation, PageSize + 1));
    assert(!HideSoPrivate::IsReadableAddress(0, 1));
    assert(!HideSoPrivate::IsReadableAddress(UINTPTR_MAX, 2));

    int Protection = 0;
    assert(HideSoPrivate::GetAddressProtection((uintptr_t)Reservation, &Protection));
    assert(Protection == PROT_READ);
    assert(HideSoPrivate::GetAddressProtection((uintptr_t)Reservation + PageSize, &Protection));
    assert(Protection == PROT_NONE);
    assert(!HideSoPrivate::GetAddressProtection((uintptr_t)Reservation, nullptr));

    HideSoPrivate::ImageIdentity Identity = {};
    HideSo::Status Status = HideSoPrivate::FindImageIdentity((const void*)&puts, &Identity);
    printf("Image lookup status: %d\n", (int)Status);
    assert(Status == HideSo::Success);
    assert(Identity.Base != 0 && Identity.Path[0] != '\0');
    assert(HideSoPrivate::HasImagePath(Identity));

    HideSoPrivate::MemoryMapList ImageMappings;
    assert(HideSoPrivate::CollectImageMappings(Identity, &ImageMappings) == HideSo::Success);
    assert(ImageMappings.Count > 1);
    assert(HideSoPrivate::CollectImageMappings(Identity, nullptr) == HideSo::InvalidArgument);
    HideSoPrivate::ImageIdentity MissingIdentity = {};
    assert(!HideSoPrivate::HasImagePath(MissingIdentity));
    assert(HideSoPrivate::CollectImageMappings(MissingIdentity, &ImageMappings) == HideSo::ImageNotFound);
    assert(ImageMappings.Count == 0);
    TestFragmentedImage(PageSize);
    assert(munmap(Reservation, ReservationSize) == 0);
    puts("PASS: map-heavy lookup, growable image collection, remap preflight, content and permissions");
}
