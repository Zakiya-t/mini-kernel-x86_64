/* =============================================================================
 * elf_loader.c — ELF64 Executable Loader for x86-64 MiniKernel
 *
 * This is the kernel's equivalent of Linux's fs/binfmt_elf.c.
 * It is called by SYS_EXEC to load a binary from MiniFS into RAM and
 * redirect the calling process's execution to the ELF's entry point.
 *
 * Loading sequence:
 *   1. Open ELF file on MiniFS disk
 *   2. Read & validate 64-byte ELF file header (Elf64_Ehdr)
 *   3. For each PT_LOAD program header:
 *        a. Allocate physical RAM frames (PMM)
 *        b. Map frames into process address space (VMM)
 *        c. Copy segment bytes from disk into RAM
 *        d. Zero the .bss region (memsz > filesz)
 *   4. Allocate a fresh user stack
 *   5. Redirect the process interrupt frame → Ring 3 entry point
 *
 * When SYS_EXEC returns to user space, the CPU jumps to the ELF's _start.
 * ============================================================================= */

#include "exec/elf_loader.h"
#include "elf.h"
#include "fs/fs.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "process/process.h"
#include "core/kprintf.h"
#include "string.h"
#include "types.h"

// #define ELF_DEBUG 1
#ifdef ELF_DEBUG
#define ELF_LOG_INFO(...) KLOG_INFO(__VA_ARGS__)
#define ELF_LOG_OK(...)   KLOG_OK(__VA_ARGS__)
#else
#define ELF_LOG_INFO(...) ((void)0)
#define ELF_LOG_OK(...)   ((void)0)
#endif

/* ── Internal Helpers ──────────────────────────────────────────────────────── */

/* Align address UP to the next 4 KiB page boundary */
static inline uint64_t page_align_up(uint64_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/* Align address DOWN to the previous 4 KiB page boundary */
static inline uint64_t page_align_down(uint64_t addr) {
    return addr & ~(PAGE_SIZE - 1);
}

/* ── Error String Table ────────────────────────────────────────────────────── */
const char *elf_error_string(int err) {
    switch (err) {
        case ELF_OK:            return "Success";
        case ELF_ERR_NOT_FOUND: return "File not found on MiniFS";
        case ELF_ERR_TOO_LARGE: return "ELF file exceeds maximum load size (4 MiB)";
        case ELF_ERR_BAD_MAGIC: return "Not an ELF file (bad magic bytes)";
        case ELF_ERR_NOT_64BIT: return "ELF is not 64-bit (ELFCLASS64 required)";
        case ELF_ERR_NOT_EXEC:  return "ELF is not an executable (ET_EXEC required)";
        case ELF_ERR_NOT_X86:   return "ELF is not x86-64 (EM_X86_64 required)";
        case ELF_ERR_NO_LOAD:   return "ELF has no PT_LOAD segments";
        case ELF_ERR_NO_MEM:    return "Physical memory allocation failed";
        case ELF_ERR_BAD_ADDR:  return "Segment virtual address is out of user range";
        case ELF_ERR_READ:      return "Disk read error while reading ELF segment";
        default:                return "Unknown ELF loader error";
    }
}

/* ── Main ELF Loader ───────────────────────────────────────────────────────── */

int elf_load(const char *path, process_t *proc, interrupt_frame_t *frame, int argc, const char *const argv[]) {
    if (!path || !proc || !frame) return ELF_ERR_NOT_FOUND;

    char kpath[128];
    strncpy(kpath, path, sizeof(kpath) - 1);
    kpath[sizeof(kpath) - 1] = '\0';

    ELF_LOG_INFO("ELF Loader: Loading \"%s\" for PID %u [\"%s\"]",
              kpath, proc->pid, proc->name);

    /* ── Step 1: Check the file exists on MiniFS ─────────────────────────── */
    fs_stat_t st;
    if (fs_stat(kpath, &st) != 0) {
        KLOG_ERR("ELF Loader: File \"%s\" not found on MiniFS", kpath);
        return ELF_ERR_NOT_FOUND;
    }

    if (st.type != FS_TYPE_FILE) {
        KLOG_ERR("ELF Loader: \"%s\" is a directory, not an executable", path);
        return ELF_ERR_NOT_FOUND;
    }

    if (st.size > ELF_MAX_FILE_SIZE) {
        KLOG_ERR("ELF Loader: \"%s\" is too large (%u bytes, max 4 MiB)", path, st.size);
        return ELF_ERR_TOO_LARGE;
    }

    /* ── Step 2: Open the ELF file on disk ───────────────────────────────── */
    int fd = fs_open(path, O_RDONLY);
    if (fd < 0) {
        KLOG_ERR("ELF Loader: Failed to open \"%s\" (error %d)", path, fd);
        return ELF_ERR_NOT_FOUND;
    }

    /* ── Step 3: Read & validate the 64-byte ELF file header ────────────── */
    Elf64_Ehdr ehdr;
    int n = fs_read(fd, &ehdr, sizeof(Elf64_Ehdr));
    if (n != (int)sizeof(Elf64_Ehdr)) {
        KLOG_ERR("ELF Loader: Failed to read ELF header from \"%s\"", path);
        fs_close(fd);
        return ELF_ERR_READ;
    }

    /* Validate the ELF header (magic, class, data, type, machine) */
    int valid = elf64_check_valid(&ehdr);
    if (valid != 0) {
        KLOG_ERR("ELF Loader: \"%s\" failed validation (code %d: %s)",
                 path, valid, elf_error_string(valid - 1));
        fs_close(fd);
        return valid - 1;   /* maps -1..-6 to our ELF_ERR_* codes */
    }

    ELF_LOG_OK("ELF Loader: Valid ELF64 x86-64 executable detected");
    ELF_LOG_INFO("ELF Loader:   Entry Point  : 0x%016lx", ehdr.e_entry);
    ELF_LOG_INFO("ELF Loader:   Program Hdrs : %u (each %u bytes at offset 0x%lx)",
              ehdr.e_phnum, ehdr.e_phentsize, ehdr.e_phoff);

    /* ── Step 4: Read all Program Headers ───────────────────────────────── */
    if (ehdr.e_phnum == 0 || ehdr.e_phnum > 32) {
        KLOG_ERR("ELF Loader: Invalid program header count: %u", ehdr.e_phnum);
        fs_close(fd);
        return ELF_ERR_NO_LOAD;
    }

    uint32_t phdr_bytes = ehdr.e_phnum * sizeof(Elf64_Phdr);

    /* Seek to program header table offset by reading and discarding */
    /* Our MiniFS fd offset tracks position; we need to read from e_phoff */
    /* Use fs_read_file to read the whole file into a temporary kernel heap buffer */
    fs_close(fd);

    /* Allocate a kernel heap buffer to hold the full ELF image */
    /* We use the kernel heap (kmalloc equivalent = heap_alloc via pmm) */
    /* For simplicity: read the whole ELF into RAM via fs_read_file     */
    static uint8_t elf_load_buf[ELF_MAX_FILE_SIZE];  /* 4 MiB static buffer in .bss */

    int bytes_read = fs_read_file(path, elf_load_buf, ELF_MAX_FILE_SIZE);
    if (bytes_read <= 0 || (uint32_t)bytes_read < sizeof(Elf64_Ehdr)) {
        KLOG_ERR("ELF Loader: Failed to read full ELF image from \"%s\"", path);
        return ELF_ERR_READ;
    }

    ELF_LOG_INFO("ELF Loader: Read %d bytes of ELF image into kernel buffer", bytes_read);

    /* Re-read header from buffer */
    Elf64_Ehdr *hdr = (Elf64_Ehdr *)elf_load_buf;

    /* Validate program header offset is within the file */
    if (hdr->e_phoff + phdr_bytes > (uint64_t)bytes_read) {
        KLOG_ERR("ELF Loader: Program header table extends beyond file bounds");
        return ELF_ERR_READ;
    }

    /* Point to program headers inside the buffer */
    Elf64_Phdr *ph = (Elf64_Phdr *)(elf_load_buf + hdr->e_phoff);

    /* ── Step 5: Load PT_LOAD segments ──────────────────────────────────── */
    int load_count = 0;
    uint64_t highest_vaddr = 0;

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        /* Skip non-loadable segments (PT_GNU_STACK, PT_NOTE, etc.) */
        if (ph[i].p_type != PT_LOAD) {
            ELF_LOG_INFO("ELF Loader:   Segment [%u]: type=0x%x (skipped)", i, ph[i].p_type);
            continue;
        }

        uint64_t vaddr   = ph[i].p_vaddr;
        uint64_t filesz  = ph[i].p_filesz;
        uint64_t memsz   = ph[i].p_memsz;
        uint64_t offset  = ph[i].p_offset;
        uint32_t flags   = ph[i].p_flags;

        /* Validate virtual address is in user space (below 128 TiB canonical limit) */
        if (vaddr == 0 || vaddr >= 0x0000800000000000ULL) {
            KLOG_ERR("ELF Loader: Segment [%u] vaddr 0x%016lx is out of user space", i, vaddr);
            return ELF_ERR_BAD_ADDR;
        }

        /* Validate segment data is within the file buffer */
        if (offset + filesz > (uint64_t)bytes_read) {
            KLOG_ERR("ELF Loader: Segment [%u] extends beyond file (offset=%lu filesz=%lu)",
                     i, offset, filesz);
            return ELF_ERR_READ;
        }

        /* Calculate the number of 4 KiB pages this segment occupies */
        uint64_t seg_start  = page_align_down(vaddr);
        uint64_t seg_end    = page_align_up(vaddr + memsz);
        uint64_t num_pages  = (seg_end - seg_start) / PAGE_SIZE;

        if (num_pages == 0) num_pages = 1;

        ELF_LOG_INFO("ELF Loader:   PT_LOAD [%u]: vaddr=0x%016lx memsz=%lu filesz=%lu pages=%lu flags=%c%c%c",
                  i, vaddr, memsz, filesz, num_pages,
                  (flags & PF_R) ? 'R' : '-',
                  (flags & PF_W) ? 'W' : '-',
                  (flags & PF_X) ? 'X' : '-');

        /* Allocate contiguous physical RAM frames for this segment */
        uint64_t phys_base = pmm_alloc_frames((size_t)num_pages);
        if (!phys_base) {
            KLOG_ERR("ELF Loader: Out of physical memory loading segment [%u]", i);
            return ELF_ERR_NO_MEM;
        }

        /* Zero all allocated frames first (clears .bss and padding) */
        memset((void *)phys_base, 0, num_pages * PAGE_SIZE);

        /* Copy file bytes into the physical frames at the correct offset */
        if (filesz > 0) {
            /* The segment data in RAM starts at: phys_base + (vaddr - seg_start) */
            uint64_t copy_dest = phys_base + (vaddr - seg_start);
            memcpy((void *)copy_dest, elf_load_buf + offset, (size_t)filesz);
        }

        /* Map each physical page into the process's virtual address space */
        /* Page flags: PRESENT + (WRITABLE if PF_W) + USER (always Ring 3) */
        uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
        if (flags & PF_W) page_flags |= PAGE_WRITABLE;

        for (uint64_t p = 0; p < num_pages; p++) {
            uint64_t virt_page = seg_start + p * PAGE_SIZE;
            uint64_t phys_page = phys_base + p * PAGE_SIZE;
            int map_res = vmm_map_page(proc->pml4, virt_page, phys_page, page_flags);
            if (map_res != 0) {
                KLOG_ERR("ELF Loader: vmm_map_page failed for virt 0x%016lx (err %d)",
                         virt_page, map_res);
                pmm_free_frames(phys_base, (size_t)num_pages);
                return ELF_ERR_NO_MEM;
            }
            /* Flush TLB for this virtual page */
            invlpg(virt_page);
        }

        ELF_LOG_OK("ELF Loader:   Segment [%u] mapped: phys=0x%016lx virt=0x%016lx (%lu pages)",
                i, phys_base, seg_start, num_pages);

        /* Track highest virtual address for stack placement */
        if (vaddr + memsz > highest_vaddr) {
            highest_vaddr = vaddr + memsz;
        }

        load_count++;
    }

    if (load_count == 0) {
        KLOG_ERR("ELF Loader: No PT_LOAD segments found in \"%s\"", path);
        return ELF_ERR_NO_LOAD;
    }

    ELF_LOG_OK("ELF Loader: Loaded %d segments, highest vaddr: 0x%016lx", load_count, highest_vaddr);

    /* ── Step 6: Set up a fresh user stack for the new program ──────────── */
    uint64_t new_rsp = proc->user_stack_top - 16; /* 16-byte aligned below top */

    /* Clear the user stack region to avoid leaking kernel data */
    uint64_t stack_virt = proc->user_stack_top - 4 * PAGE_SIZE;
    uint64_t top_page_phys = 0;

    for (size_t s = 0; s < 4; s++) {
        uint64_t phys = vmm_virt_to_phys(proc->pml4, stack_virt + s * PAGE_SIZE);
        if (phys) {
            memset((void *)phys, 0, PAGE_SIZE);
            if (s == 3) {
                top_page_phys = phys; /* Page containing user_stack_top */
            }
        }
    }

    uint64_t user_argv_ptr = 0;
    if (argc > 0 && argv && top_page_phys) {
        uint8_t *top_page = (uint8_t *)top_page_phys;
        uint32_t top_offset = PAGE_SIZE;

        uint64_t user_str_vaddrs[32];
        int max_args = (argc > 31) ? 31 : argc;

        for (int i = max_args - 1; i >= 0; i--) {
            size_t slen = strlen(argv[i]) + 1;
            if (top_offset < slen) break;
            top_offset -= (uint32_t)slen;
            memcpy(top_page + top_offset, argv[i], slen);
            user_str_vaddrs[i] = (proc->user_stack_top - PAGE_SIZE) + top_offset;
        }

        top_offset &= ~0xFULL; /* 16-byte align */

        uint32_t table_entries = 1 + 1 + max_args + 1; /* envp NULL, argv NULL, argv[0..max-1], argc */
        uint32_t table_bytes = table_entries * sizeof(uint64_t);

        if (top_offset >= table_bytes) {
            top_offset -= table_bytes;
            top_offset &= ~0xFULL;

            uint64_t *table = (uint64_t *)(top_page + top_offset);
            uint32_t idx = 0;

            table[idx++] = (uint64_t)max_args;
            user_argv_ptr = (proc->user_stack_top - PAGE_SIZE) + top_offset + 8;

            for (int i = 0; i < max_args; i++) {
                table[idx++] = user_str_vaddrs[i];
            }
            table[idx++] = 0; /* argv[argc] = NULL */
            table[idx++] = 0; /* envp[0] = NULL */

            new_rsp = (proc->user_stack_top - PAGE_SIZE) + top_offset;
        }
    }

    /* ── Step 7: Redirect the process interrupt frame to ELF entry point ── */
    frame->rip    = hdr->e_entry;         /* ELF _start address              */
    frame->rsp    = new_rsp;              /* Fresh user stack pointer         */
    frame->rbp    = new_rsp;             /* Base pointer = stack top         */
    frame->cs     = 0x23;                /* User Code Segment DPL=3          */
    frame->ss     = 0x1B;                /* User Data Segment DPL=3          */
    frame->rflags = 0x202;               /* Interrupts enabled (IF=1)        */

    /* Clear general-purpose registers and set up ABI argument registers */
    frame->rax = 0; frame->rbx = 0; frame->rcx = 0; frame->rdx = 0;
    frame->rdi = (uint64_t)argc;
    frame->rsi = user_argv_ptr ? user_argv_ptr : (new_rsp + 8);
    frame->r8  = 0; frame->r9  = 0; frame->r10 = 0; frame->r11 = 0;
    frame->r12 = 0; frame->r13 = 0; frame->r14 = 0; frame->r15 = 0;

    /* Update process name to the loaded program */
    const char *basename = kpath;
    for (const char *p = kpath; *p; p++) {
        if (*p == '/') basename = p + 1;
    }
    strncpy(proc->name, basename, sizeof(proc->name) - 1);
    proc->name[sizeof(proc->name) - 1] = '\0';

    ELF_LOG_OK("ELF Loader: SUCCESS — PID %u will execute \"%s\" at 0x%016lx (Ring 3)",
            proc->pid, kpath, hdr->e_entry);
    ELF_LOG_INFO("ELF Loader:   New RIP=0x%016lx RSP=0x%016lx CS=0x%x SS=0x%x",
              frame->rip, frame->rsp, frame->cs, frame->ss);

    proc->state = PROC_READY;
    return ELF_OK;
}

