#include "types.h"
#include "string.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/pit.h"
#include "core/kprintf.h"
#include "core/multiboot.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "syscall/syscall.h"
#include "process/process.h"
#include "drivers/ata.h"
#include "fs/fs.h"
#include "exec/elf_loader.h"

extern uint8_t kernel_start[];
extern uint8_t kernel_end[];

void kmain(uint64_t magic, uint64_t mb_addr) {
    /* 1. Early serial console */
    serial_init();

    /* 2. VGA text console */
    vga_init();

    /* 3. Welcome Banner */
    kprintf("\n");
    kprintf("===============================================================================\n");
    kprintf("          WELCOME TO MINI-OS (x86-64 Long Mode)                    \n");
    kprintf("       A Real, Bootable Operating System for OS Learners & Teachers            \n");
    kprintf("===============================================================================\n");
    kprintf(" Architecture: Pure x86-64 | Privilege: Ring 0 Kernel + Ring 3 User Isolation \n");
    kprintf(" Hardware: 512 MiB RAM | 1 GiB Virtual Disk | 16550A Serial | 8254 PIT (100 Hz)\n");
    kprintf("-------------------------------------------------------------------------------\n\n");

    /* 4. Hardware Initialization */
    KLOG_HW("Initializing Core Architecture Subsystems...");
    multiboot_dump_info(magic, mb_addr);
    gdt_init();
    idt_init();
    pic_init();
    pit_init(100);

    /* 5. Memory Management Initialization */
    KLOG_HW("Initializing Real Memory Management Subsystems (PMM, VMM, Heap)...");
    pmm_init(mb_addr, (uint64_t)kernel_end);
    vmm_init();
    heap_init(16 * 1024 * 1024); // 16 MiB dynamic kernel heap

    /* 6. System Call Subsystem */
    KLOG_HW("Initializing System Call Subsystem (int 0x80)...");
    syscall_init();

    /* 7. Process Management & Scheduler */
    KLOG_HW("Initializing Process Management Subsystem...");
    process_init();

    /* 8. Storage & Persistent Inode Filesystem Subsystem */
    KLOG_HW("Initializing Storage & Persistent Filesystem Subsystem...");
    fs_init();

    /* 9. Create Process 1: Independent User Shell (Loaded from MiniFS Disk) */
    KLOG_HW("Spawning Process 1: Loading /bin/myshell from ATA MiniFS disk (Ring 3)...");
    process_t *shell_proc = process_create("myshell", NULL, 5, 3);
    if (!shell_proc) {
        KLOG_ERR("PANIC: Failed to create PID 1 process!");
        while (1) { hlt(); }
    }

    const char *sh_argv[] = { "/bin/myshell", NULL };
    int elf_res = elf_load("/bin/myshell", shell_proc, &shell_proc->context, 1, sh_argv);
    if (elf_res != ELF_OK) {
        KLOG_ERR("PANIC: Failed to load /bin/myshell from disk: %s", elf_error_string(elf_res));
        while (1) { hlt(); }
    }

    KLOG_OK("Loaded /bin/myshell: Entry point 0x%016lx | Privilege: Ring 3 User Mode",
            shell_proc->context.rip);

    /* 10. Enable CPU Interrupts */
    KLOG_HW("Enabling CPU Interrupts ('sti')...");
    sti();
    KLOG_OK("Hardware interrupts active (IF flag set in RFLAGS)");

    KLOG_OK("Entering User Mode (Ring 3) via hardware IRETQ transition...\n");

    /* 11. Switch CPU to Ring 3 and start MyShell */
    process_enter_user_mode(shell_proc);

    /* If user shell ever exits, halt CPU safely */
    while (1) {
        hlt();
    }
}
