; ==============================================================================
; context_switch.asm - Low-level privilege switch to Ring 3 in x86-64
; ==============================================================================

section .text
bits 64
global enter_user_mode

; ------------------------------------------------------------------------------
; enter_user_mode(uint64_t rip, uint64_t user_rsp, uint64_t user_rdi, uint64_t user_rsi)
;   rdi = User instruction pointer (entry point)
;   rsi = User stack pointer (top of user stack)
;   rdx = User argument passed in rdi (argc)
;   rcx = User argument passed in rsi (argv)
; ------------------------------------------------------------------------------
enter_user_mode:
    ; Load data segment registers with User Data Selector (0x18 | 3 = 0x1B)
    mov ax, 0x1B
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Construct 64-bit IRETQ frame on current stack:
    ;   [RSP + 32] = SS     (User Data: 0x1B)
    ;   [RSP + 24] = RSP    (User Stack Pointer: RSI)
    ;   [RSP + 16] = RFLAGS (IF=1, IOPL=0: 0x202)
    ;   [RSP + 8]  = CS     (User Code: 0x20 | 3 = 0x23)
    ;   [RSP + 0]  = RIP    (User Entry Point: RDI)

    push qword 0x1B             ; SS: User Data Segment
    push rsi                    ; RSP: User Stack Pointer
    push qword 0x202            ; RFLAGS: Interrupt flag enabled (0x200 | 0x02)
    push qword 0x23             ; CS: User Code Segment (Ring 3 DPL=3)
    push rdi                    ; RIP: User Code Entry Point

    ; Pass user arguments in RDI (argc) and RSI (argv)
    mov rdi, rdx
    mov rsi, rcx
    xor rdx, rdx
    xor rcx, rcx
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    xor rbp, rbp

    ; Hardware privilege switch: CPU drops to Ring 3, restores RSP & RIP!
    iretq
