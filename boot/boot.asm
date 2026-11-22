; ==============================================================================
; boot.asm - x86-64 Multiboot Trampoline and Long Mode Initialization
; ==============================================================================
; Educational MiniKernel - Step 1: Hardware Initialization & Booting
;
; Purpose:
;   1. Complies with the Multiboot specification (loaded by GRUB / QEMU -kernel)
;   2. Validates CPU capabilities (CPUID, 64-bit Long Mode support)
;   3. Configures early 4-level paging (PML4 -> PDPT -> PD with 2MiB huge pages)
;      identity mapping the first 1 GiB of physical memory.
;   4. Activates PAE and IA32_EFER.LME (Long Mode Enable)
;   5. Switches CPU from 32-bit Protected Mode into 64-bit Sub-Mode (Long Mode)
;   6. Sets up a 64-bit stack and transfers control to the C kernel (kmain)
; ==============================================================================

section .multiboot
align 4
    MB_ALIGN     equ 1 << 0
    MB_MEMINFO   equ 1 << 1
    MB_FLAGS     equ MB_ALIGN | MB_MEMINFO
    MB_MAGIC     equ 0x1BADB002
    MB_CHECKSUM  equ -(MB_MAGIC + MB_FLAGS)

    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .text
bits 32
global _start
extern kmain

_start:
    ; Disable interrupts immediately
    cli

    ; Initialize temporary stack pointer to early stack
    mov esp, stack_top

    ; Multiboot verification:
    ; Bootloader places magic 0x2BADB002 in EAX, multiboot info address in EBX
    cmp eax, 0x2BADB002
    jne error_no_multiboot

    ; Save multiboot parameters
    mov [mb_magic_saved], eax
    mov [mb_info_saved], ebx

    ; Verify CPU support for CPUID instruction
    call check_cpuid

    ; Verify CPU support for x86-64 Long Mode
    call check_long_mode

    ; Set up 4-level identity paging (PML4, PDPT, PD)
    call setup_page_tables

    ; Enable Physical Address Extension (PAE) in CR4
    call enable_pae

    ; Enable Long Mode in Extended Feature Enable Register (IA32_EFER)
    call enable_long_mode_msr

    ; Enable Paging in CR0 (enters IA-32e compatibility mode)
    call enable_paging

    ; Load 64-bit Global Descriptor Table
    lgdt [gdt64_descriptor]

    ; Far jump into 64-bit code segment (0x08) to switch to true 64-bit Long Mode
    jmp 0x08:long_mode_entry

; ------------------------------------------------------------------------------
; Check if CPUID instruction is supported
; ------------------------------------------------------------------------------
check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je error_no_cpuid
    ret

; ------------------------------------------------------------------------------
; Check if CPU supports 64-bit Long Mode
; ------------------------------------------------------------------------------
check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb error_no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz error_no_long_mode
    ret

; ------------------------------------------------------------------------------
; Early Page Table Setup
; Identity maps 0x00000000 to 0x40000000 (First 1 GiB of RAM)
; Uses 2 MiB huge pages (512 entries in Page Directory)
; ------------------------------------------------------------------------------
setup_page_tables:
    ; pml4_table[0] -> pdpt_table (flags: Present | Writable = 0x03)
    mov eax, pdpt_table
    or eax, 0x03
    mov [pml4_table], eax

    ; pdpt_table[0] -> pd_table (flags: Present | Writable = 0x03)
    mov eax, pd_table
    or eax, 0x03
    mov [pdpt_table], eax

    ; Fill 512 entries in pd_table (512 * 2 MiB = 1 GiB)
    ; Flags: Present (0x01) | Writable (0x02) | Huge Page (0x80) = 0x83
    mov ecx, 0
.map_pd_loop:
    mov eax, 0x200000           ; 2 MiB
    mul ecx                     ; EAX = ecx * 2 MiB
    or eax, 0x83                ; Present, Writable, 2MiB Huge Page
    mov [pd_table + ecx * 8], eax
    mov dword [pd_table + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .map_pd_loop

    ret

; ------------------------------------------------------------------------------
; Enable PAE (Physical Address Extension) in CR4
; ------------------------------------------------------------------------------
enable_pae:
    mov eax, cr4
    or eax, 1 << 5              ; Bit 5 = PAE
    mov cr4, eax
    ret

; ------------------------------------------------------------------------------
; Enable Long Mode in IA32_EFER MSR
; ------------------------------------------------------------------------------
enable_long_mode_msr:
    ; Point CR3 to PML4 base address
    mov eax, pml4_table
    mov cr3, eax

    ; Read IA32_EFER MSR (0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8              ; Bit 8 = LME (Long Mode Enable)
    wrmsr
    ret

; ------------------------------------------------------------------------------
; Enable Paging in CR0
; ------------------------------------------------------------------------------
enable_paging:
    mov eax, cr0
    or eax, (1 << 31) | (1 << 0) ; Bit 31 = PG (Paging), Bit 0 = PE (Protection)
    mov cr0, eax
    ret

; ------------------------------------------------------------------------------
; Boot Error Handlers
; ------------------------------------------------------------------------------
error_no_multiboot:
    mov al, 'M'
    jmp error_halt

error_no_cpuid:
    mov al, 'C'
    jmp error_halt

error_no_long_mode:
    mov al, 'L'
    jmp error_halt

error_halt:
    mov dword [0xB8000], 0x4F524F45 ; "ER" in red on white
    mov dword [0xB8004], 0x4F3A4F52 ; "R:" in red on white
    mov byte  [0xB8008], al         ; Error code character ('M', 'C', 'L')
    mov byte  [0xB8009], 0x4F
    cli
.hang:
    hlt
    jmp .hang

; ------------------------------------------------------------------------------
; 64-bit Sub-Mode Entry Point
; ------------------------------------------------------------------------------
bits 64
default rel
long_mode_entry:
    ; Reload data segment selectors with 64-bit data selector (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Enable SSE instructions (CR0.MP=1, CR0.EM=0, CR4.OSFXSR=1, CR4.OSXMMEXCPT=1)
    mov rax, cr0
    and ax, 0xFFFB              ; Clear EM (bit 2) - do not emulate x87/SSE
    or ax, 0x0002               ; Set MP (bit 1) - monitor coprocessor
    mov cr0, rax

    mov rax, cr4
    or eax, (1 << 9) | (1 << 10) ; Bit 9 = OSFXSR, Bit 10 = OSXMMEXCPT
    mov cr4, rax

    ; Initialize 64-bit kernel stack
    lea rsp, [stack_top]
    xor rbp, rbp

    ; Prepare arguments according to System V AMD64 ABI:
    ;   rdi = 1st arg: Multiboot magic
    ;   rsi = 2nd arg: Multiboot info structure physical pointer
    mov edi, [mb_magic_saved]
    mov esi, [mb_info_saved]

    ; Transfer execution to C kernel entry point
    call kmain

    ; If kmain returns, halt the CPU safely
    cli
.halt_loop:
    hlt
    jmp .halt_loop

; ==============================================================================
; Early GDT for 64-bit Transition
; ==============================================================================
section .rodata
align 8
gdt64:
    dq 0                        ; 0x00: Null descriptor
.code: equ $ - gdt64
    ; 0x08: 64-bit Kernel Code Descriptor
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.data: equ $ - gdt64
    ; 0x10: 64-bit Kernel Data Descriptor
    dq (1 << 41) | (1 << 44) | (1 << 47)

gdt64_descriptor:
    dw $ - gdt64 - 1
    dq gdt64

; ==============================================================================
; BSS Data: Page Tables, Saved Boot Info, Stack
; ==============================================================================
section .bss
alignb 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_table:
    resb 4096

alignb 16
mb_magic_saved:
    resd 1
mb_info_saved:
    resd 1

; 64 KiB Kernel Execution Stack
alignb 16
stack_bottom:
    resb 65536
stack_top:
