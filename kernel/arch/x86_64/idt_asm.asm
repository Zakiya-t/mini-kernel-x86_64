; ==============================================================================
; idt_asm.asm - Assembly ISR Stubs and Interrupt Dispatching for x86-64
; ==============================================================================

section .text
bits 64
extern isr_dispatch

; Macro for exceptions that do NOT push an error code
%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0                ; Push dummy error code
    push qword %1               ; Push interrupt vector number
    jmp isr_common_stub
%endmacro

; Macro for exceptions that DO push an error code
%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1               ; Push interrupt vector number (error code already on stack)
    jmp isr_common_stub
%endmacro

; Macro for hardware IRQs (vectors 32-47)
%macro IRQ_STUB 2
global irq%1
irq%1:
    push qword 0                ; Push dummy error code
    push qword %2               ; Push mapped interrupt vector number
    jmp isr_common_stub
%endmacro

; ------------------------------------------------------------------------------
; CPU Exception Stubs (0-31)
; ------------------------------------------------------------------------------
ISR_NOERRCODE 0   ; 0: Divide-by-Zero
ISR_NOERRCODE 1   ; 1: Debug
ISR_NOERRCODE 2   ; 2: Non-Maskable Interrupt
ISR_NOERRCODE 3   ; 3: Breakpoint
ISR_NOERRCODE 4   ; 4: Overflow
ISR_NOERRCODE 5   ; 5: Bound Range Exceeded
ISR_NOERRCODE 6   ; 6: Invalid Opcode
ISR_NOERRCODE 7   ; 7: Device Not Available
ISR_ERRCODE   8   ; 8: Double Fault (with error code)
ISR_NOERRCODE 9   ; 9: Coprocessor Segment Overrun
ISR_ERRCODE   10  ; 10: Invalid TSS (with error code)
ISR_ERRCODE   11  ; 11: Segment Not Present (with error code)
ISR_ERRCODE   12  ; 12: Stack-Segment Fault (with error code)
ISR_ERRCODE   13  ; 13: General Protection Fault (with error code)
ISR_ERRCODE   14  ; 14: Page Fault (with error code)
ISR_NOERRCODE 15  ; 15: Reserved
ISR_NOERRCODE 16  ; 16: x87 Floating-Point Exception
ISR_ERRCODE   17  ; 17: Alignment Check (with error code)
ISR_NOERRCODE 18  ; 18: Machine Check
ISR_NOERRCODE 19  ; 19: SIMD Floating-Point Exception
ISR_NOERRCODE 20  ; 20: Virtualization Exception
ISR_ERRCODE   21  ; 21: Control Protection Exception (with error code)
ISR_NOERRCODE 22  ; 22: Reserved
ISR_NOERRCODE 23  ; 23: Reserved
ISR_NOERRCODE 24  ; 24: Reserved
ISR_NOERRCODE 25  ; 25: Reserved
ISR_NOERRCODE 26  ; 26: Reserved
ISR_NOERRCODE 27  ; 27: Reserved
ISR_NOERRCODE 28  ; 28: Hypervisor Injection Exception
ISR_ERRCODE   29  ; 29: VMM Communication Exception (with error code)
ISR_ERRCODE   30  ; 30: Security Exception (with error code)
ISR_NOERRCODE 31  ; 31: Reserved

; ------------------------------------------------------------------------------
; Hardware IRQ Stubs (remapped to vectors 32-47)
; ------------------------------------------------------------------------------
IRQ_STUB 0,  32   ; IRQ0: Programmable Interval Timer (PIT)
IRQ_STUB 1,  33   ; IRQ1: PS/2 Keyboard
IRQ_STUB 2,  34   ; IRQ2: Cascade
IRQ_STUB 3,  35   ; IRQ3: COM2 Serial
IRQ_STUB 4,  36   ; IRQ4: COM1 Serial
IRQ_STUB 5,  37   ; IRQ5: LPT2 / Sound
IRQ_STUB 6,  38   ; IRQ6: Floppy Disk
IRQ_STUB 7,  39   ; IRQ7: LPT1 / Spurious
IRQ_STUB 8,  40   ; IRQ8: Real Time Clock (RTC)
IRQ_STUB 9,  41   ; IRQ9: ACPI / Free
IRQ_STUB 10, 42   ; IRQ10: Free
IRQ_STUB 11, 43   ; IRQ11: Free
IRQ_STUB 12, 44   ; IRQ12: PS/2 Mouse
IRQ_STUB 13, 45   ; IRQ13: FPU Coprocessor
IRQ_STUB 14, 46   ; IRQ14: Primary ATA Hard Disk
IRQ_STUB 15, 47   ; IRQ15: Secondary ATA Hard Disk

; ------------------------------------------------------------------------------
; Software Syscall Interrupt Stub (Vector 0x80 = 128)
; ------------------------------------------------------------------------------
ISR_NOERRCODE 128 ; Syscall gate

; ------------------------------------------------------------------------------
; Common ISR / IRQ Dispatch Stub
; Saves all general-purpose registers, calls C handler, restores registers, iretq
; ------------------------------------------------------------------------------
isr_common_stub:
    ; Push general-purpose registers in order matching interrupt_frame_t
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    ; Pass pointer to interrupt frame (current RSP) in RDI (AMD64 ABI 1st arg)
    mov rdi, rsp

    ; Ensure direction flag is forward for C code
    cld

    ; Dispatch to C interrupt handler
    call isr_dispatch

    ; Restore general-purpose registers
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; Pop int_no (8 bytes) and err_code (8 bytes)
    add rsp, 16

    ; Return from interrupt (restores RIP, CS, RFLAGS, RSP, SS)
    iretq

; ------------------------------------------------------------------------------
; Load IDT pointer via lidt instruction
; ------------------------------------------------------------------------------
global idt_load
idt_load:
    lidt [rdi]
    ret
