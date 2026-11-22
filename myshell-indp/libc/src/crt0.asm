bits 64
section .text
global _start
extern main
extern exit

_start:
    ; Retrieve argc and argv
    test rdi, rdi
    jnz .have_args
    mov rdi, [rsp]
    lea rsi, [rsp + 8]

.have_args:
    ; Align stack to 16 bytes for System V AMD64 ABI
    and rsp, -16
    call main

    ; Exit with main's return value
    mov rdi, rax
    call exit

.halt:
    mov rax, 1 ; SYS_EXIT
    int 0x80
    jmp .halt

