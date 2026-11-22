#ifndef _ARCH_X86_64_IDT_H
#define _ARCH_X86_64_IDT_H

#include "types.h"

/* 64-bit Interrupt Frame passed to C handlers */
typedef struct __attribute__((packed)) {
    /* Pushed by assembly stub (general purpose registers in reverse order) */
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    /* Pushed by assembly stub */
    uint64_t int_no;
    uint64_t err_code;

    /* Pushed automatically by x86-64 CPU hardware */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

/* IDT Entry (Gate Descriptor) in x86-64 Long Mode (16 bytes) */
typedef struct __attribute__((packed)) {
    uint16_t offset_low;    /* Bits 0..15 of handler function address */
    uint16_t selector;      /* Code segment selector in GDT (0x08) */
    uint8_t  ist;           /* Bits 0..2: IST index (0 = none), bits 3..7: zero */
    uint8_t  type_attr;     /* Gate type, DPL, and P flag */
    uint16_t offset_mid;    /* Bits 16..31 of handler function address */
    uint32_t offset_high;   /* Bits 32..63 of handler function address */
    uint32_t zero;          /* Reserved, must be 0 */
} idt_entry_t;

/* IDT Pointer structure for lidt instruction */
typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

typedef void (*irq_handler_t)(interrupt_frame_t *frame);

void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void irq_register_handler(uint8_t irq, irq_handler_t handler);

#endif /* _ARCH_X86_64_IDT_H */
