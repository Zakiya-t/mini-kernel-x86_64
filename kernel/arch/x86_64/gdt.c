#include "arch/x86_64/gdt.h"
#include "core/kprintf.h"

extern void gdt_flush(gdt_ptr_t *ptr);
extern void tss_flush(void);

/* Dedicated 16 KiB interrupt/syscall stack for Ring 0 switches */
static uint8_t kernel_tss_stack[16384] __attribute__((aligned(16)));

/*
 * GDT Layout in x86-64:
 * Each standard descriptor is exactly 8 bytes (64 bits).
 * In 64-bit mode, the TSS descriptor is a System Descriptor that occupies
 * TWO consecutive 8-byte slots (16 bytes total).
 *
 * Index 0 (0x00): Null Descriptor
 * Index 1 (0x08): Kernel Code 64 (Ring 0)
 * Index 2 (0x10): Kernel Data 64 (Ring 0)
 * Index 3 (0x18): User Data 64 (Ring 3, selector 0x1B)
 * Index 4 (0x20): User Code 64 (Ring 3, selector 0x23)
 * Index 5 (0x28): TSS Descriptor Low 8 bytes
 * Index 6 (0x30): TSS Descriptor High 8 bytes
 */
static gdt_entry_t gdt_table[7] __attribute__((aligned(16)));
static gdt_ptr_t gdt_ptr;
static tss_entry_t kernel_tss __attribute__((aligned(16)));

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_table[num].base_low    = (base & 0xFFFF);
    gdt_table[num].base_middle = (base >> 16) & 0xFF;
    gdt_table[num].base_high   = (base >> 24) & 0xFF;

    gdt_table[num].limit_low   = (limit & 0xFFFF);
    gdt_table[num].granularity = (limit >> 16) & 0x0F;
    gdt_table[num].granularity |= gran & 0xF0;
    gdt_table[num].access      = access;
}

static void gdt_set_tss_gate(int num, uint64_t base, uint32_t limit) {
    /* Low 8 bytes at gdt_table[num] */
    gdt_table[num].limit_low    = (limit & 0xFFFF);
    gdt_table[num].base_low     = (base & 0xFFFF);
    gdt_table[num].base_middle  = (base >> 16) & 0xFF;
    gdt_table[num].access       = 0x89; /* Present, Ring 0, 64-bit TSS Available */
    gdt_table[num].granularity  = (limit >> 16) & 0x0F;
    gdt_table[num].base_high    = (base >> 24) & 0xFF;

    /* High 8 bytes at gdt_table[num + 1]: upper 32-bits of base and reserved zero */
    uint64_t *upper = (uint64_t *)&gdt_table[num + 1];
    *upper = (base >> 32) & 0xFFFFFFFF;
}

void gdt_set_kernel_stack(uint64_t stack_top) {
    kernel_tss.rsp0 = stack_top;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt_table) - 1;
    gdt_ptr.base  = (uint64_t)&gdt_table[0];

    /* 0x00: Null Descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 0x08: Kernel Code Segment (64-bit, Ring 0, Executable, Readable) */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0x20);

    /* 0x10: Kernel Data Segment (64-bit, Ring 0, Writable) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0x00);

    /* 0x18: User Data Segment (64-bit, Ring 3, Writable) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xF2, 0x00);

    /* 0x20: User Code Segment (64-bit, Ring 3, Executable, Readable) */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0x20);

    /* Initialize Task State Segment (TSS) */
    for (size_t i = 0; i < sizeof(kernel_tss); i++) {
        ((uint8_t *)&kernel_tss)[i] = 0;
    }

    /* Set default RSP0 to top of allocated kernel TSS stack */
    kernel_tss.rsp0 = (uint64_t)kernel_tss_stack + sizeof(kernel_tss_stack);
    kernel_tss.iopb_offset = sizeof(kernel_tss); // No I/O permissions for Ring 3

    /* 0x28 (and 0x30): 64-bit TSS System Descriptor (occupies slots 5 and 6) */
    gdt_set_tss_gate(5, (uint64_t)&kernel_tss, sizeof(kernel_tss) - 1);

    /* Load GDT and flush segment registers */
    gdt_flush(&gdt_ptr);

    /* Load Task Register (TR) */
    tss_flush();

    KLOG_OK("GDT & TSS initialized (Kernel Ring 0 + User Ring 3 + TSS RSP0: 0x%016lx)",
            kernel_tss.rsp0);
}
