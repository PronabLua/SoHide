#include "ElfFile.hpp"
#include "HideSo/xorstr.hpp"

#include <elf.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace HideSoPrivate {
    static bool MatchesHeadSymbol(const char* SymbolName)
    {
        return strncmp(SymbolName, xorstr_("__dl__ZL6solist"), sizeof("__dl__ZL6solist") - 1) == 0 || strncmp(SymbolName, xorstr_("__dl__ZL11solist_head"), sizeof("__dl__ZL11solist_head") - 1) == 0;
    }

    static bool MatchesTailSymbol(const char* SymbolName)
    {
        return strncmp(SymbolName, xorstr_("__dl__ZL6sonext"), sizeof("__dl__ZL6sonext") - 1) == 0 || strncmp(SymbolName, xorstr_("__dl__ZL11solist_tail"), sizeof("__dl__ZL11solist_tail") - 1) == 0;
    }

    static bool IsRangeValid(size_t FileSize, uint64_t Offset, uint64_t Length)
    {
        return Offset <= FileSize && Length <= FileSize - Offset;
    }

    HideSo::Status FindLinkerSymbols(uintptr_t LinkerBase, const char* LinkerPath, LinkerSymbols* Symbols)
    {
        if (LinkerBase == 0 || LinkerPath == nullptr || LinkerPath[0] == '\0' || Symbols == nullptr)
        {
            return HideSo::InvalidArgument;
        }

        Symbols->ListHeadStorage = 0;
        Symbols->ListTailStorage = 0;

        int LinkerFile = open(LinkerPath, O_RDONLY | O_CLOEXEC);
        if (LinkerFile < 0)
        {
            return HideSo::LinkerSymbolsUnavailable;
        }

        struct stat LinkerFileStatus = {};
        if (fstat(LinkerFile, &LinkerFileStatus) != 0 || LinkerFileStatus.st_size <= 0)
        {
            close(LinkerFile);
            return HideSo::LinkerSymbolsUnavailable;
        }

        size_t LinkerFileSize = (size_t)LinkerFileStatus.st_size;
        void* LinkerFileMapping = mmap(nullptr, LinkerFileSize, PROT_READ, MAP_PRIVATE, LinkerFile, 0);
        close(LinkerFile);
        if (LinkerFileMapping == MAP_FAILED)
        {
            return HideSo::LinkerSymbolsUnavailable;
        }

        const Elf64_Ehdr* Header = (const Elf64_Ehdr*)LinkerFileMapping;
        if (LinkerFileSize < sizeof(Elf64_Ehdr) || memcmp(Header->e_ident, ELFMAG, SELFMAG) != 0 || Header->e_ident[EI_CLASS] != ELFCLASS64 || Header->e_machine != EM_AARCH64)
        {
            munmap(LinkerFileMapping, LinkerFileSize);
            return HideSo::LinkerSymbolsUnavailable;
        }

        if (Header->e_shentsize != sizeof(Elf64_Shdr) || Header->e_shnum == 0 || !IsRangeValid(LinkerFileSize, Header->e_shoff, (uint64_t)Header->e_shnum * sizeof(Elf64_Shdr)))
        {
            munmap(LinkerFileMapping, LinkerFileSize);
            return HideSo::LinkerSymbolsUnavailable;
        }

        const Elf64_Shdr* Sections = (const Elf64_Shdr*)((const uint8_t*)LinkerFileMapping + Header->e_shoff);
        for (size_t SectionIndex = 0; SectionIndex < Header->e_shnum; ++SectionIndex)
        {
            const Elf64_Shdr& SymbolSection = Sections[SectionIndex];
            if (SymbolSection.sh_type != SHT_SYMTAB || SymbolSection.sh_entsize != sizeof(Elf64_Sym) || SymbolSection.sh_link >= Header->e_shnum)
            {
                continue;
            }

            const Elf64_Shdr& StringSection = Sections[SymbolSection.sh_link];
            if (!IsRangeValid(LinkerFileSize, SymbolSection.sh_offset, SymbolSection.sh_size) || !IsRangeValid(LinkerFileSize, StringSection.sh_offset, StringSection.sh_size))
            {
                continue;
            }

            const Elf64_Sym* SymbolTable = (const Elf64_Sym*)((const uint8_t*)LinkerFileMapping + SymbolSection.sh_offset);
            const char* StringTable = (const char*)LinkerFileMapping + StringSection.sh_offset;
            size_t SymbolCount = SymbolSection.sh_size / sizeof(Elf64_Sym);
            for (size_t SymbolIndex = 0; SymbolIndex < SymbolCount; ++SymbolIndex)
            {
                const Elf64_Sym& Symbol = SymbolTable[SymbolIndex];
                if (Symbol.st_name >= StringSection.sh_size)
                {
                    continue;
                }

                const char* SymbolName = StringTable + Symbol.st_name;
                if (MatchesHeadSymbol(SymbolName))
                {
                    Symbols->ListHeadStorage = LinkerBase + Symbol.st_value;
                }
                else if (MatchesTailSymbol(SymbolName))
                {
                    Symbols->ListTailStorage = LinkerBase + Symbol.st_value;
                }
            }
        }

        munmap(LinkerFileMapping, LinkerFileSize);
        return Symbols->ListHeadStorage != 0 && Symbols->ListTailStorage != 0 ? HideSo::Success : HideSo::LinkerSymbolsUnavailable;
    }
}
