/* ==============================================================================
 * linker.ld - Linker script for x86-64 Educational MiniKernel
 * ============================================================================== */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /* Load kernel at 1 MiB physical address (standard for Multiboot kernels) */
    . = 1M;

    kernel_start = .;

    /* Multiboot header must appear within the first 8 KiB */
    .multiboot ALIGN(4K) :
    {
        KEEP(*(.multiboot))
    }

    /* Kernel Code (.text) */
    .text ALIGN(4K) :
    {
        *(.text .text.*)
    }

    /* Read-Only Data (.rodata) */
    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    /* Initialized Data (.data) */
    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    /* Uninitialized Data (.bss) */
    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4K);
    kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame)
    }
}
