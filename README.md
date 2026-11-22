# Unified Educational MiniKernel (x86-64)

> A real, small, bootable educational operating system built in C and x86-64 assembly with live kernel observability. Designed for computer science students and teachers to explore how an OS works under the hood.

---

## Target System Specifications
- **Architecture**: Pure `x86-64` (Intel 64 / AMD64 Long Mode)
- **Privilege Separation**: Ring 0 (Kernel) vs Ring 3 (User Shell)
- **Host Development Environment**: Ubuntu on WSL2 / Linux
- **Emulator**: QEMU System x86-64 (`qemu-system-x86_64`)
- **Guest RAM**: `512 MiB`
- **Virtual Disk**: `1 GiB` persistent storage (`disk.img`)
- **Console**: Serial UART 16550A COM1 (`0x3F8`) + VGA Text Mode (`0xB8000`)
- **Bootloader**: Multiboot Specification (GRUB 2.x ISO)

---

## Project Structure

```
.
├── Makefile                     # Pure 64-bit build rules, ISO generation, and QEMU runners
├── linker.ld                    # ELF64 linker script with 4 KiB page alignment
├── disk.img                     # 1 GiB virtual persistent disk image
├── boot/
│   └── boot.asm                 # Multiboot header, CPUID validation, early 4-level paging,
│                                # PAE/EFER long mode enable, and 32->64 trampoline
├── include/
│   ├── types.h                  # Freestanding C types (uint8_t..uint64_t, bool)
│   ├── string.h                 # Kernel memory and string functions header
│   ├── syscall_nums.h           # Shared syscall IDs and structs between user & kernel
│   ├── stdint.h, stddef.h       # Freestanding headers
│   └── stdbool.h
├── kernel/
│   ├── arch/x86_64/
│   │   ├── io.h                 # Port I/O assembly inline functions (inb, outb, etc.)
│   │   ├── cpu.h                # CPU registers (CR0, CR2, CR3, CR4, MSRs, CPUID)
│   │   ├── serial.h / serial.c  # UART 16550A driver on COM1 (0x3F8) @ 38400 baud, 8N1
│   │   ├── vga.h / vga.c        # VGA 80x25 text mode driver (0xB8000)
│   │   ├── gdt.h / gdt.c        # 64-bit GDT (Ring 0 & Ring 3) + TSS with RSP0 stack
│   │   ├── gdt_asm.asm          # Assembly lgdt, segment register reload, and ltr
│   │   ├── idt.h / idt.c        # 64-bit IDT (32 exceptions, 16 IRQs, 0x80 syscall gate)
│   │   ├── idt_asm.asm          # Assembly ISR stubs and register dump frame
│   │   ├── pic.h / pic.c        # 8259 PIC remapping (Master: 0x20..0x27, Slave: 0x28..0x2F)
│   │   └── pit.h / pit.c        # 8254 PIT timer driver (100 Hz, 10 ms ticks)
│   ├── memory/
│   │   ├── pmm.h / pmm.c        # Bitmap Physical Memory Manager (4 KiB page frames)
│   │   ├── vmm.h / vmm.c        # 4-Level Paging Virtual Memory Manager (PML4, PDPT, PD, PT)
│   │   └── heap.h / heap.c      # Dynamic Kernel Heap (16 MiB, boundary-tag block allocator)
│   ├── process/
│   │   ├── process.h / process.c # Process Control Block (PCB), ready queue, 3 schedulers
│   │   └── context_switch.asm   # Hardware IRETQ transition to Ring 3
│   ├── syscall/
│   │   ├── syscall.h / syscall.c # System Call Dispatcher (int 0x80) & Live Observer
│   └── core/
│       ├── string.c             # Freestanding memset, memcpy, memcmp, strcpy, etc.
│       ├── kprintf.h / kprintf.c # Formatted logging engine (%s, %d, %u, %x, %p) to serial & screen
│       ├── multiboot.h / multiboot.c # Multiboot 1 parser for hardware memory map and RAM detection
│       └── kmain.c              # Clean kernel bootstrap, spawns Process 1 into Ring 3
├── user/                        # USER SPACE (Ring 3 - User Mode)
│   ├── lib/
│   │   ├── syscall.h            # User syscall wrapper prototypes & formatting
│   │   └── syscall.c            # Inline assembly invoking int 0x80
│   └── shell.h / shell.c        # Interactive User Shell running in Ring 3
└── iso/boot/grub/grub.cfg       # GRUB bootloader configuration
```

---

## Interactive User Shell Commands (Ring 3)

Type these commands directly in the shell:

| Command | Action | Real Kernel Mechanism |
| :--- | :--- | :--- |
| `whoami` | Inspect CPU privilege level | Reads CPU `CS` register to prove execution in **Ring 3 (User Mode)** |
| `mem` | View real memory usage | Issues `SYS_MEM_STATS` syscall to retrieve PMM and Heap stats |
| `alloc <bytes>` | Allocate dynamic memory | Issues `SYS_MEM_ALLOC` syscall; verifies written pattern |
| `free <hex_ptr>` | Release dynamic memory | Issues `SYS_MEM_FREE` syscall with double-free defense |
| `test mem` | Run memory test suite | Issues `SYS_TEST_MEM` syscall; executes kernel tests on demand |
| `sched` | View current scheduler | Issues `SYS_SCHED_GET` syscall (Preemptive Round Robin) |
| `sched rr` | Confirm Round Robin scheduler | Issues `SYS_SCHED_SET` syscall (Quantum = 50ms / 5 ticks) |
| `ps` | List processes | Issues `SYS_PROC_LIST` syscall; displays PIDs, states, ticks, rings |
| `observe on` | Enable Live Observer | Issues `SYS_OBSERVE` syscall; prints live request lifecycles |
| `observe off` | Disable Live Observer | Disables request lifecycle tracing |
| `yield` | Voluntary CPU yield | Issues `SYS_YIELD` syscall to give control to the scheduler |
| `clear` | Clear terminal | ANSI display reset |

---

## How to Build & Run in WSL Ubuntu

In your Ubuntu terminal inside `/mnt/e/kernel-shell`:

```bash
# 1. Build the pure 64-bit kernel and bootable ISO
make

# 2. Launch the interactive User Shell in QEMU
make qemu
```
*(Press `Ctrl+A` then `X` to exit QEMU).*
