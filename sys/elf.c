// elf.c - ELF file loader
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef ELF_TRACE
#define TRACE
#endif

#ifdef ELF_DEBUG
#define DEBUG
#endif

#include "elf.h"
#include "conf.h"
#include "io.h"
#include "string.h"
#include "memory.h"
#include "assert.h"
#include "error.h"

#include <stdint.h>

// INTERNEL CONSTANT DEFINITIONS
//

// Offsets into e_ident

#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7
#define EI_ABIVERSION   8
#define EI_PAD          9

// ELF header e_ident[EI_CLASS] values

#define ELFCLASSNONE    0
#define ELFCLASS32      1
#define ELFCLASS64      2

// ELF header e_ident[EI_DATA] values

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// ELF header e_ident[EI_VERSION] values

#define EV_NONE     0
#define EV_CURRENT  1

// Program header p_flags bits

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

// ELF header e_machine values (short list)

#define EM_RISCV 243

// INTERNAL TYPE DEFINITIONS
//

// ELF header e_type values

enum elf_et
{
    ET_NONE = 0,
    ET_REL,
    ET_EXEC,
    ET_DYN,
    ET_CORE
};

struct elf64_ehdr
{
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

enum elf_pt
{
    PT_NULL = 0,
    PT_LOAD,
    PT_DYNAMIC,
    PT_INTERP,
    PT_NOTE,
    PT_SHLIB,
    PT_PHDR,
    PT_TLS
};

struct elf64_phdr
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

// EXPORTED FUNCTION DEFINITIONS
//

int elf_load(struct io * elfio, void (**eptr)(void))
{
    struct elf64_ehdr elf_header;
    struct elf64_phdr prog_header;
    int result;

    result = ioseek(elfio, 0);

    if (result < 0)
    {
        kprintf("Error: %d\n", result);
        panic("Failed to set position of io device");
    }

    result = ioread(elfio, &elf_header, sizeof(elf_header));

    if (result < 0)
    {
        kprintf("Error: %d\n", result);
        panic("Failed to read first 64 bytes from io device");
    }

    debug("size=%lld", sizeof(elf_header));
    debug("e_ident: ");
    debug("%x", elf_header.e_ident[0]);
    debug("%x", elf_header.e_ident[1]);
    debug("%x", elf_header.e_ident[2]);
    debug("%x", elf_header.e_ident[3]);
    debug("%x", elf_header.e_ident[4]);
    debug("%x", elf_header.e_ident[5]);
    debug("%x", elf_header.e_ident[6]);
    debug("%x", elf_header.e_ident[7]);
    debug("%x", elf_header.e_ident[8]);
    debug("%x", elf_header.e_ident[9]);
    debug("%x", elf_header.e_ident[10]);
    debug("%x", elf_header.e_ident[11]);
    debug("%x", elf_header.e_ident[12]);
    debug("%x", elf_header.e_ident[13]);
    debug("%x", elf_header.e_ident[14]);
    debug("%x", elf_header.e_ident[15]);
    debug("e_type: %x", elf_header.e_type);
    debug("e_machine: %x", elf_header.e_machine);
    debug("e_version: %x", elf_header.e_version);
    debug("e_entry: %x", elf_header.e_entry);
    debug("e_phoff: %x", elf_header.e_phoff);
    debug("e_shoff: %x", elf_header.e_shoff);
    debug("e_flags: %x", elf_header.e_flags);
    debug("e_ehsisz: %x", elf_header.e_ehsize);
    debug("e_phentsize: %x", elf_header.e_phentsize);
    debug("e_phnum: %x", elf_header.e_phnum);
    debug("e_shentsize: %x", elf_header.e_shentsize);
    debug("e_shnum: %x", elf_header.e_shnum);
    debug("e_shstrndx: %x", elf_header.e_shstrndx);

    if (elf_header.e_ident[0] != 0x7f ||
        elf_header.e_ident[1] != 'E'  ||
        elf_header.e_ident[2] != 'L'  ||
        elf_header.e_ident[3] != 'F'  ||
        elf_header.e_ident[EI_CLASS] != ELFCLASS64 ||
        elf_header.e_ident[EI_DATA] != ELFDATA2LSB ||
        elf_header.e_ident[EI_VERSION] != EV_CURRENT ||
        elf_header.e_type != ET_EXEC ||
        elf_header.e_machine != EM_RISCV ||
        elf_header.e_version != 1)
    {
        return -EINVAL;
    }

    // maybe need OSABI, ABIVERSION, e_type check
    // might need to make macros for magic numbers
    *eptr = (void *) elf_header.e_entry;
    uint64_t curr_offset;

    for (uint32_t i = 0; i < elf_header.e_phnum; i++)
    {
        curr_offset = elf_header.e_phoff + (elf_header.e_phentsize * i);

        debug("\n");
        debug("current offset=%d", curr_offset);

        ioseek(elfio, curr_offset);
        ioread(elfio, &prog_header, elf_header.e_phentsize);

        debug("p_type: %x", prog_header.p_type);
        debug("p_flags: %x", prog_header.p_flags);
        debug("p_offset: %x", prog_header.p_offset);
        debug("p_vaddr: %x", prog_header.p_vaddr);
        debug("p_paddr: %x", prog_header.p_paddr);
        debug("p_filesz: %x", prog_header.p_filesz);
        debug("p_memsz: %x", prog_header.p_memsz);
        debug("p_align: %x", prog_header.p_align);

        if (prog_header.p_type != PT_LOAD)
        {
            debug("shamoned");
            continue;
        }

        // verify in user memory space
        if (prog_header.p_vaddr < UMEM_START_VMA ||
            prog_header.p_vaddr + prog_header.p_memsz >= UMEM_END_VMA)
        {
            return -EINVAL;
        }

        // allocate and map page tables for user space

        void * prog_vp = alloc_and_map_range (
                prog_header.p_vaddr, prog_header.p_memsz,
                PTE_R | PTE_W | PTE_U);

        ioseek(elfio, prog_header.p_offset);
        ioread(elfio, prog_vp, prog_header.p_filesz);

        // zeros out the values after filesz
        // acts as a placeholder for uninitialized static variables
        //memset ((char *)(prog_header.p_filesz + prog_vp),
        //        0, prog_header.p_memsz);
        for (uint64_t j = prog_header.p_filesz; j < prog_header.p_memsz; j++)
        {
            *((char *)(j + prog_vp)) = 0;
        }

        uint_fast8_t flags = PTE_U;

        if ((prog_header.p_flags & PF_X) != 0)
            flags |= PTE_X;

        if ((prog_header.p_flags & PF_W) != 0)
            flags |= PTE_W;

        if ((prog_header.p_flags & PF_R) != 0)
            flags |= PTE_R;

        debug("loaded into: %x to %x", prog_header.p_vaddr, prog_header.p_vaddr
                + prog_header.p_memsz);
        debug("execute flag: %d", flags & PTE_X);
        debug("write flag: %d", flags & PTE_W);
        debug("read flag: %d", flags & PTE_R);

        set_range_flags(prog_vp, prog_header.p_memsz, flags);
    }

    return 0;
}
