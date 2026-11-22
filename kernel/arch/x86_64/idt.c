#include "arch/x86_64/idt.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/cpu.h"
#include "core/kprintf.h"

#define IDT_MAX_ENTRIES 256

static idt_entry_t idt_entries[IDT_MAX_ENTRIES] __attribute__((aligned(16)));
static idt_ptr_t   idt_ptr;
static irq_handler_t irq_handlers[16] = {0};

extern void idt_load(idt_ptr_t *ptr);

/* Assembly ISR stubs declared in idt_asm.asm */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

extern void isr128(void);

static const char *exception_messages[32] = {
    "#DE: Divide-by-Zero Error",
    "#DB: Debug Exception",
    "NMI: Non-Maskable Interrupt",
    "#BP: Breakpoint Exception",
    "#OF: Overflow Exception",
    "#BR: Bound Range Exceeded",
    "#UD: Invalid Opcode (Undefined Opcode)",
    "#NM: Device Not Available (No Math Coprocessor)",
    "#DF: Double Fault",
    "Coprocessor Segment Overrun",
    "#TS: Invalid TSS",
    "#NP: Segment Not Present",
    "#SS: Stack-Segment Fault",
    "#GP: General Protection Fault",
    "#PF: Page Fault",
    "Reserved",
    "#MF: x87 Floating-Point Exception",
    "#AC: Alignment Check",
    "#MC: Machine Check",
    "#XM: SIMD Floating-Point Exception",
    "#VE: Virtualization Exception",
    "#CP: Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved",
    "#HV: Hypervisor Injection Exception",
    "#VC: VMM Communication Exception",
    "#SX: Security Exception",
    "Reserved"
};

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].offset_low  = (uint16_t)(base & 0xFFFF);
    idt_entries[num].selector    = sel;
    idt_entries[num].ist         = 0;
    idt_entries[num].type_attr   = flags;
    idt_entries[num].offset_mid  = (uint16_t)((base >> 16) & 0xFFFF);
    idt_entries[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt_entries[num].zero        = 0;
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entries) - 1;
    idt_ptr.base  = (uint64_t)&idt_entries;

    for (size_t i = 0; i < IDT_MAX_ENTRIES; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    /* CPU Exceptions 0-31 (Ring 0 Gate = 0x8E: Present, DPL 0, 64-bit Interrupt Gate) */
    idt_set_gate(0,  (uint64_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint64_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint64_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint64_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint64_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint64_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint64_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint64_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint64_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint64_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);

    /* Hardware IRQs 0-15 mapped to vectors 32-47 */
    idt_set_gate(32, (uint64_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint64_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint64_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint64_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint64_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint64_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint64_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint64_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint64_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint64_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E);

    /* System call gate vector 128 (0x80) with DPL 3 (0xEE) for Ring 3 invocation */
    idt_set_gate(128, (uint64_t)isr128, 0x08, 0xEE);

    /* Load IDT into CPU */
    idt_load(&idt_ptr);

    KLOG_OK("64-bit IDT installed (32 Exceptions + 16 IRQs + Ring 3 Syscall Gate 0x80)");
}

static void print_register_dump(interrupt_frame_t *f) {
    kprintf("  RIP: 0x%016lx  CS:  0x%04x  RFLAGS: 0x%016lx\n", f->rip, (uint16_t)f->cs, f->rflags);
    kprintf("  RSP: 0x%016lx  SS:  0x%04x\n", f->rsp, (uint16_t)f->ss);
    kprintf("  RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n", f->rax, f->rbx, f->rcx);
    kprintf("  RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n", f->rdx, f->rsi, f->rdi);
    kprintf("  RBP: 0x%016lx  R8:  0x%016lx  R9:  0x%016lx\n", f->rbp, f->r8, f->r9);
    kprintf("  R10: 0x%016lx  R11: 0x%016lx  R12: 0x%016lx\n", f->r10, f->r11, f->r12);
    kprintf("  R13: 0x%016lx  R14: 0x%016lx  R15: 0x%016lx\n", f->r13, f->r14, f->r15);
}

void isr_dispatch(interrupt_frame_t *frame) {
    /* 1. CPU Exception handling */
    if (frame->int_no < 32) {
        kprintf("\n=======================================================\n");
        kprintf("!!! CPU EXCEPTION %u: %s !!!\n", (uint32_t)frame->int_no, exception_messages[frame->int_no]);
        kprintf("=======================================================\n");
        kprintf("  Error Code: 0x%016lx\n", frame->err_code);

        if (frame->int_no == 14) {
            uint64_t cr2 = read_cr2();
            kprintf("  Page Fault Linear Address (CR2): 0x%016lx\n", cr2);
            kprintf("  Cause: %s, %s, %s, %s\n",
                    (frame->err_code & 1) ? "Protection Violation (Present)" : "Page Not Present",
                    (frame->err_code & 2) ? "Write Access" : "Read Access",
                    (frame->err_code & 4) ? "User Mode (Ring 3)" : "Kernel Mode (Ring 0)",
                    (frame->err_code & 16) ? "Instruction Fetch" : "Data Access");
        }

        print_register_dump(frame);
        kprintf("=======================================================\n");

        /* If exception occurred in Kernel mode (Ring 0), halt kernel */
        if ((frame->cs & 3) == 0) {
            kprintf("[PANIC] Kernel halted due to unhandled CPU exception.\n");
            cli();
            while (1) { hlt(); }
        }
        return;
    }

    /* 2. Hardware IRQ handling (Vectors 32 to 47) */
    if (frame->int_no >= 32 && frame->int_no < 48) {
        uint8_t irq = (uint8_t)(frame->int_no - 32);
        if (irq_handlers[irq]) {
            irq_handlers[irq](frame);
        }
        pic_send_eoi(irq);
        return;
    }

    /* 3. Syscall Interrupt Gate (Vector 128 / 0x80) */
    if (frame->int_no == 128) {
        extern void syscall_dispatcher(interrupt_frame_t *frame);
        syscall_dispatcher(frame);
        return;
    }

    KLOG_WARN("Unhandled interrupt vector %u", (uint32_t)frame->int_no);
}
