#ifndef _ARCH_X86_64_GDT_H
#define _ARCH_X86_64_GDT_H

#include "types.h"

#define GDT_KERNEL_CODE_SEG 0x08
#define GDT_KERNEL_DATA_SEG 0x10
#define GDT_USER_DATA_SEG   (0x18 | 3)
#define GDT_USER_CODE_SEG   (0x20 | 3)
#define GDT_TSS_SEG         0x28

/* x86-64 Task State Segment (TSS) */
typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0;       /* Stack pointer for Ring 0 switch */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;       /* Interrupt Stack Table entries (1-7) */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset; /* I/O permission bit map base */
} tss_entry_t;

/* GDT entry structure */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} gdt_entry_t;

/* 64-bit System Descriptor (used for TSS in Long Mode, 16 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper32;
    uint32_t reserved;
} gdt_system_entry_t;

/* GDT pointer structure for lgdt instruction */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack_top);

#endif /* _ARCH_X86_64_GDT_H */
