; ==============================================================================
; gdt_asm.asm - Assembly helpers for loading GDT and TSS in x86-64
; ==============================================================================

section .text
bits 64
global gdt_flush
global tss_flush

; ------------------------------------------------------------------------------
; gdt_flush(gdt_ptr_t *ptr)
; rdi = pointer to gdt_ptr_t
; ------------------------------------------------------------------------------
gdt_flush:
    lgdt [rdi]

    ; Reload kernel data segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Reload code segment register (CS) via 64-bit far return (retfq)
    push 0x08                   ; Kernel code selector
    lea rax, [rel .flush_cs]    ; RIP-relative address of continuation label
    push rax
    retfq

.flush_cs:
    ret

; ------------------------------------------------------------------------------
; tss_flush()
; Loads Task Register (TR) with TSS selector 0x28
; ------------------------------------------------------------------------------
tss_flush:
    mov ax, 0x28                ; TSS descriptor selector in GDT
    ltr ax                      ; Load Task Register
    ret
