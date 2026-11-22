/* =============================================================================
 * elf.h — Standard ELF64 Structures for x86-64 MiniKernel ELF Loader
 *
 * References the System V AMD64 ABI and ELF-64 Object File Format spec.
 * Only the fields needed to load a static ELF64 executable are included.
 * ============================================================================= */

#ifndef _INCLUDE_ELF_H
#define _INCLUDE_ELF_H

#include "types.h"

/* ── ELF Magic & Identification ───────────────────────────────────────────── */
#define ELF_MAGIC0          0x7F
#define ELF_MAGIC1          'E'
#define ELF_MAGIC2          'L'
#define ELF_MAGIC3          'F'

/* e_ident indices */
#define EI_MAG0             0
#define EI_MAG1             1
#define EI_MAG2             2
#define EI_MAG3             3
#define EI_CLASS            4   /* 1=32-bit, 2=64-bit        */
#define EI_DATA             5   /* 1=LSB (little-endian)     */
#define EI_VERSION          6   /* must be 1 (EV_CURRENT)    */
#define EI_OSABI            7
#define EI_NIDENT           16

/* EI_CLASS values */
#define ELFCLASS64          2

/* EI_DATA values */
#define ELFDATA2LSB         1   /* little-endian             */

/* ── ELF File Types (e_type) ─────────────────────────────────────────────── */
#define ET_EXEC             2   /* Executable file           */

/* ── ELF Machine Types (e_machine) ──────────────────────────────────────── */
#define EM_X86_64           62  /* AMD x86-64                */

/* ── ELF Version (e_version) ────────────────────────────────────────────── */
#define EV_CURRENT          1

/* ── Program Header Types (p_type) ──────────────────────────────────────── */
#define PT_NULL             0   /* Unused entry              */
#define PT_LOAD             1   /* Loadable segment          */
#define PT_DYNAMIC          2   /* Dynamic linking info      */
#define PT_INTERP           3   /* Path to interpreter       */
#define PT_NOTE             4
#define PT_GNU_STACK        0x6474e551
#define PT_GNU_RELRO        0x6474e552

/* ── Program Header Flags (p_flags) ─────────────────────────────────────── */
#define PF_X                0x1  /* Execute                  */
#define PF_W                0x2  /* Write                    */
#define PF_R                0x4  /* Read                     */

/* ============================================================================
 * ELF64 File Header — 64 bytes at the very start of every ELF file
 * ============================================================================ */
typedef struct {
    uint8_t  e_ident[EI_NIDENT]; /* Magic + class + data + version + OS ABI  */
    uint16_t e_type;             /* ET_EXEC = 2 for executables               */
    uint16_t e_machine;          /* EM_X86_64 = 62                            */
    uint32_t e_version;          /* EV_CURRENT = 1                            */
    uint64_t e_entry;            /* Virtual address of program entry (_start) */
    uint64_t e_phoff;            /* Byte offset to first Program Header       */
    uint64_t e_shoff;            /* Byte offset to first Section Header       */
    uint32_t e_flags;            /* Processor-specific flags (0 for x86-64)   */
    uint16_t e_ehsize;           /* Size of THIS header (64 bytes)            */
    uint16_t e_phentsize;        /* Size of ONE Program Header entry (56 bytes)*/
    uint16_t e_phnum;            /* Number of Program Header entries          */
    uint16_t e_shentsize;        /* Size of ONE Section Header entry          */
    uint16_t e_shnum;            /* Number of Section Header entries          */
    uint16_t e_shstrndx;         /* Section header string table index         */
} __attribute__((packed)) Elf64_Ehdr;

/* ============================================================================
 * ELF64 Program Header — 56 bytes, one per loadable segment
 * ============================================================================ */
typedef struct {
    uint32_t p_type;    /* PT_LOAD = 1 → this segment gets loaded into RAM   */
    uint32_t p_flags;   /* PF_R, PF_W, PF_X  permission flags                */
    uint64_t p_offset;  /* Byte offset in ELF file where segment data starts  */
    uint64_t p_vaddr;   /* Virtual address where segment should be mapped     */
    uint64_t p_paddr;   /* Physical address (unused for user-space ELFs)      */
    uint64_t p_filesz;  /* Bytes to copy from file (may be 0 for .bss)        */
    uint64_t p_memsz;   /* Bytes to allocate in memory (>= p_filesz for .bss) */
    uint64_t p_align;   /* Alignment (must be power of 2; 0 or 1 = none)     */
} __attribute__((packed)) Elf64_Phdr;

/* ============================================================================
 * Validation Helpers
 * ============================================================================ */
static inline int elf64_check_magic(const Elf64_Ehdr *hdr) {
    return (hdr->e_ident[EI_MAG0] == ELF_MAGIC0 &&
            hdr->e_ident[EI_MAG1] == ELF_MAGIC1 &&
            hdr->e_ident[EI_MAG2] == ELF_MAGIC2 &&
            hdr->e_ident[EI_MAG3] == ELF_MAGIC3) ? 0 : -1;
}

static inline int elf64_check_valid(const Elf64_Ehdr *hdr) {
    if (elf64_check_magic(hdr) != 0)              return -1; /* Bad magic     */
    if (hdr->e_ident[EI_CLASS]   != ELFCLASS64)   return -2; /* Not 64-bit   */
    if (hdr->e_ident[EI_DATA]    != ELFDATA2LSB)   return -3; /* Not LSB      */
    if (hdr->e_ident[EI_VERSION] != EV_CURRENT)   return -4; /* Bad version  */
    if (hdr->e_type              != ET_EXEC)       return -5; /* Not exec     */
    if (hdr->e_machine           != EM_X86_64)     return -6; /* Not x86-64   */
    if (hdr->e_phnum             == 0)             return -7; /* No segments  */
    return 0;
}

#endif /* _INCLUDE_ELF_H */

