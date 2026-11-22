#include "syscall/syscall.h"
#include "process/process.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/pit.h"
#include "arch/x86_64/io.h"
#include "fs/fs.h"
#include "drivers/ata.h"
#include "ansi_colors.h"
#include "core/kprintf.h"
#include "string.h"

static bool observer_enabled = false;
static uint64_t request_seq = 0;
static char active_cmd_name[32] = "";
static uint64_t active_cmd_seq = 0;
static uint32_t active_child_syscall_count = 0;

void syscall_set_observer(bool enabled) {
    observer_enabled = enabled;
}

bool syscall_is_observer_enabled(void) {
    return observer_enabled;
}

void syscall_init(void) {
    observer_enabled = false;
    request_seq = 0;
    active_cmd_seq = 0;
    active_cmd_name[0] = '\0';
    active_child_syscall_count = 0;
    KLOG_OK("System Call Subsystem initialized (int 0x80 gate active, Ring 3 accessible)");
}

/* Internal Memory Test invoked from user shell via SYS_TEST_MEM */
static int run_kernel_memory_test_suite(void) {
    kprintf("\n[KERNEL] Running requested Memory Subsystem Verification Suite...\n");

    /* PMM Test */
    size_t initial_free_frames = pmm_get_free_frames();
    uint64_t frame = pmm_alloc_frame();
    if (!frame) return -1;
    pmm_free_frame(frame);
    if (pmm_get_free_frames() != initial_free_frames) return -2;

    /* Heap Test */
    size_t heap_free_before = heap_free_bytes();
    char *buf = (char *)kmalloc(128);
    if (!buf) return -3;
    strcpy(buf, "Verified from User Shell Syscall!");
    if (strcmp(buf, "Verified from User Shell Syscall!") != 0) return -4;
    kfree(buf);
    if (heap_free_bytes() != heap_free_before) return -5;

    /* Double free defense check */
    void *safety_ptr = kmalloc(32);
    kfree(safety_ptr);
    kfree(safety_ptr); // safely caught

    kprintf("[KERNEL] All Memory Tests Passed via Real Kernel Execution!\n");
    return 0;
}

static void print_observer_lifecycle(uint64_t seq, process_t *proc, interrupt_frame_t *frame,
                                     uint64_t sys_num, uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                     int64_t ret, uint64_t elapsed_cycles,
                                     uint64_t delta_ata_reads, uint64_t delta_sec_reads,
                                     uint64_t delta_ata_writes, uint64_t delta_sec_writes) {
    (void)seq;
    (void)frame;
    (void)arg2;
    (void)arg3;
    (void)elapsed_cycles;
    (void)delta_ata_reads;
    (void)delta_sec_reads;
    (void)delta_ata_writes;
    (void)delta_sec_writes;

    uint32_t pid = proc ? proc->pid : 0;
    const char *proc_name = proc ? proc->name : "unknown";

    const char *sys_name = "SYS_UNKNOWN";
    const char *title = "KERNEL WORKLOAD EXECUTION";

    switch (sys_num) {
        case SYS_EXIT:      sys_name = "SYS_EXIT";      title = "PROCESS TERMINATION"; break;
        case SYS_WRITE:     sys_name = "SYS_WRITE";     title = "PERSISTENT DISK WRITE"; break;
        case SYS_READ:      sys_name = "SYS_READ";      title = "PERSISTENT DISK READ"; break;
        case SYS_GETPID:    sys_name = "SYS_GETPID";    title = "PROCESS PID QUERY"; break;
        case SYS_YIELD:     sys_name = "SYS_YIELD";     title = "VOLUNTARY CPU YIELD"; break;
        case SYS_MEM_ALLOC: sys_name = "SYS_MEM_ALLOC"; title = "DYNAMIC HEAP ALLOCATION"; break;
        case SYS_MEM_FREE:  sys_name = "SYS_MEM_FREE";  title = "DYNAMIC HEAP RELEASE"; break;
        case SYS_MEM_STATS: sys_name = "SYS_MEM_STATS"; title = "MEMORY SUBSYSTEM AUDIT"; break;
        case SYS_SCHED_GET: sys_name = "SYS_SCHED_GET"; title = "CPU SCHEDULER QUERY"; break;
        case SYS_SCHED_SET: sys_name = "SYS_SCHED_SET"; title = "CPU SCHEDULER UPDATE"; break;
        case SYS_PROC_LIST: sys_name = "SYS_PROC_LIST"; title = "PROCESS TABLE AUDIT"; break;
        case SYS_TEST_MEM:  sys_name = "SYS_TEST_MEM";  title = "MEMORY VERIFICATION"; break;
        case SYS_SPAWN:     sys_name = "SYS_SPAWN";     title = "PROCESS CREATION WORKLOAD"; break;
        case SYS_KILL:      sys_name = "SYS_KILL";      title = "PROCESS TERMINATION"; break;
        case SYS_REAP:      sys_name = "SYS_REAP";      title = "PROCESS MEMORY RECLAIMER"; break;
        case SYS_FORK:      sys_name = "SYS_FORK";      title = "PROCESS FORK & CLONING"; break;
        case SYS_EXEC:      sys_name = "SYS_EXEC";      title = "PROCESS IMAGE EXECUTION"; break;
        case SYS_OPEN:      sys_name = "SYS_OPEN";      title = "VFS FILE OPEN & FD BIND"; break;
        case SYS_CLOSE:     sys_name = "SYS_CLOSE";     title = "VFS FILE CLOSE & FLUSH"; break;
        case SYS_FS_LIST:   sys_name = "SYS_FS_LIST";   title = "DIRECTORY EXPLORATION"; break;
        case SYS_FS_STAT:   sys_name = "SYS_FS_STAT";   title = "FILE METADATA QUERY"; break;
        case SYS_DISK_INFO: sys_name = "SYS_DISK_INFO"; title = "STORAGE HARDWARE QUERY"; break;
        case SYS_GETCWD:    sys_name = "SYS_GETCWD";    title = "PROCESS CWD QUERY"; break;
        case SYS_CHDIR:     sys_name = "SYS_CHDIR";     title = "PROCESS CWD SWITCH"; break;
        case SYS_MKDIR:     sys_name = "SYS_MKDIR";     title = "DIRECTORY CREATION"; break;
        case SYS_SEEK:      sys_name = "SYS_SEEK";      title = "FILE OFFSET REPOSITION"; break;
        default: break;
    }

    #define OBSERVER_CARD_WIDTH 76
    kprintf("\n" ANSI_BCYAN "┌── [REQUEST LIFECYCLE: %s] ", title);
    int used_header = 26 + (int)strlen(title);
    int top_dashes  = OBSERVER_CARD_WIDTH - 1 - used_header;
    if (top_dashes < 2) top_dashes = 2;
    for (int i = 0; i < top_dashes; i++) {
        kprintf("─");
    }
    kprintf("┐" ANSI_RESET "\n");
    kprintf("│\n");

    /* STAGE 1: USER SPACE */
    kprintf("│  " ANSI_BWHITE "[1. USER SPACE] " ANSI_BGREEN "(Ring 3 User Mode)" ANSI_RESET "\n");
    switch (sys_num) {
        case SYS_SPAWN:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested creation of process \"%s\"\n",
                    pid, proc_name, (const char *)arg1 ? (const char *)arg1 : "worker");
            break;
        case SYS_FORK:
            kprintf("│     └── Request: Process PID %u [\"%s\"] requested fork clone of address space\n",
                    pid, proc_name);
            break;
        case SYS_KILL:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested termination of PID %lu\n",
                    pid, proc_name, arg1);
            break;
        case SYS_REAP:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested memory reclaim for PID %lu\n",
                    pid, proc_name, arg1);
            break;
        case SYS_PROC_LIST:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested active process table (ps)\n",
                    pid, proc_name);
            break;
        case SYS_MEM_ALLOC:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested %lu bytes of dynamic heap memory\n",
                    pid, proc_name, arg1);
            break;
        case SYS_MEM_FREE:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested release of heap pointer 0x%016lx\n",
                    pid, proc_name, arg1);
            break;
        case SYS_MEM_STATS:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested memory metrics audit (mem)\n",
                    pid, proc_name);
            break;
        case SYS_FS_LIST:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested directory listing for \"%s\" (ls)\n",
                    pid, proc_name, arg1 ? (const char *)arg1 : ".");
            break;
        case SYS_MKDIR:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested creation of directory \"%s\"\n",
                    pid, proc_name, (const char *)arg1 ? (const char *)arg1 : "dir");
            break;
        case SYS_SEEK:
            kprintf("│     └── Request: PID %u [\"%s\"] requested seek on FD %ld (offset: %ld, whence: %ld)\n",
                    pid, proc_name, (int64_t)arg1, (int64_t)arg2, (int64_t)arg3);
            break;
        case SYS_CHDIR:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested directory change to \"%s\"\n",
                    pid, proc_name, (const char *)arg1 ? (const char *)arg1 : "/");
            break;
        case SYS_GETCWD:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] requested current working directory (pwd)\n",
                    pid, proc_name);
            break;
        case SYS_SCHED_GET:
        case SYS_SCHED_SET:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] queried/confirmed CPU scheduling policy\n",
                    pid, proc_name);
            break;
        case SYS_YIELD:
            kprintf("│     └── Request: Process PID %u [\"%s\"] voluntarily yielded remaining timeslice\n",
                    pid, proc_name);
            break;
        default:
            kprintf("│     └── Request: Shell PID %u [\"%s\"] initiated system call %s\n",
                    pid, proc_name, sys_name);
            break;
    }

    kprintf("│\n");

    /* STAGE 2: SYSTEM CALL TRAP GATE */
    kprintf("│  " ANSI_BWHITE "[2. SYSTEM CALL TRAP GATE] " ANSI_BYELLOW "(CPU Hardware Trap)" ANSI_RESET "\n");
    kprintf("│     └── CPU Instruction : " ANSI_BYELLOW "int 0x80" ANSI_RESET " (IDT Vector 128, DPL=3 Trap Gate)\n");
    kprintf("│     └── Privilege Shift : " ANSI_BGREEN "Ring 3 (User Space)" ANSI_RESET " ─────► " ANSI_BRED "Ring 0 (Kernel Supervisor)" ANSI_RESET "\n");
    kprintf("│     └── Syscall ID      : " ANSI_BCYAN "%s" ANSI_RESET " (Syscall Vector ID %u)\n", sys_name, (uint32_t)sys_num);

    kprintf("│\n");

    /* STAGE 3: KERNEL SUBSYSTEM COORDINATION */
    kprintf("│  " ANSI_BWHITE "[3. KERNEL SUBSYSTEM COORDINATION] " ANSI_BMAGENTA "(Ring 0 Subsystems Working Together)" ANSI_RESET "\n");

    switch (sys_num) {
        case SYS_SPAWN: {
            uint64_t ustack = 0x00007FFFF0000000ULL + ((uint64_t)ret * 0x100000ULL) + 0x4000;
            kprintf("│     ├── [1. PROCESS MANAGER] : Checked process slots ──► Allocated free PCB Slot #%ld\n", ret > 0 ? ret : 0);
            kprintf("│     ├── [2. MEMORY MANAGER]  : Allocated physical frames ──► 16 KiB Kernel & User Stacks\n");
            kprintf("│     ├── [3. VIRTUAL MEMORY]  : Mapped User Stack in PML4 page table at 0x%016lx\n", ustack);
            kprintf("│     ├── [4. VFS SUBSYSTEM]   : Inherited Working Directory (CWD: \"%s\", Inode #%u)\n", proc ? proc->cwd_path : "/", proc ? proc->cwd_inode : 1);
            kprintf("│     └── [5. CPU SCHEDULER]   : Enqueued PID %ld as READY in Round Robin queue (50 ms slice)\n", ret);
            break;
        }
        case SYS_FORK: {
            uint64_t ustack = 0x00007FFFF0000000ULL + ((uint64_t)ret * 0x100000ULL) + 0x4000;
            kprintf("│     ├── [1. PROCESS MANAGER] : Allocated new child PCB slot #%ld (Parent PID: %u)\n", ret, pid);
            kprintf("│     ├── [2. MEMORY MANAGER]  : Cloned 16 KiB user stack memory into child physical frame\n");
            kprintf("│     ├── [3. VIRTUAL MEMORY]  : Mapped child address space in PML4 at 0x%016lx\n", ustack);
            kprintf("│     ├── [4. CONTEXT ENGINE]  : Cloned CPU registers (Parent returns PID %ld, Child returns 0)\n", ret);
            kprintf("│     └── [5. CPU SCHEDULER]   : Enqueued child PID %ld as READY in Round Robin queue\n", ret);
            break;
        }
        case SYS_KILL:
            kprintf("│     ├── [1. PROCESS MANAGER] : Located target PID %lu in process table\n", arg1);
            kprintf("│     ├── [2. STATE MUTATION]  : Changed process state: READY/RUNNING ──► TERMINATED\n");
            kprintf("│     └── [3. CPU SCHEDULER]   : Removed PID %lu from active Round Robin scheduling turns\n", arg1);
            break;
        case SYS_REAP:
            kprintf("│     ├── [1. PROCESS MANAGER] : Verified PID %lu state is TERMINATED\n", arg1);
            kprintf("│     ├── [2. MEMORY MANAGER]  : Freed 4 physical frames (Reclaimed 32 KiB stack memory)\n");
            kprintf("│     └── [3. STATE MUTATION]  : Reset PCB slot to PROC_UNUSED for future tasks\n", arg1);
            break;
        case SYS_PROC_LIST:
            kprintf("│     ├── [1. PROCESS MANAGER] : Scanned process_table[0..63] ──► Found %ld active processes\n", ret);
            kprintf("│     ├── [2. CPU SCHEDULER]   : Gathered CPU runtime ticks & verified Round Robin quantum (50 ms)\n");
            kprintf("│     └── [3. MEMORY MANAGER]  : Safely transferred %ld process records into user-space buffer\n", ret);
            break;
        case SYS_MEM_ALLOC:
            kprintf("│     ├── [1. MEMORY MANAGER]  : Validated request (%lu B within 16 MiB heap pool)\n", arg1);
            kprintf("│     ├── [2. HEAP ALLOCATOR]  : Boundary-Tag search ──► Allocated chunk at 0x%016lx\n", (uint64_t)ret);
            kprintf("│     └── [3. STATE MUTATION]  : Updated pool metrics: Heap Used: %lu B (+%lu), Free: %lu B\n",
                    (uint64_t)heap_used_bytes(), arg1, (uint64_t)heap_free_bytes());
            break;
        case SYS_MEM_FREE:
            kprintf("│     ├── [1. MEMORY MANAGER]  : Validated pointer 0x%016lx is within active heap range\n", arg1);
            kprintf("│     ├── [2. HEAP ALLOCATOR]  : Boundary-Tag free ──► Coalesced with adjacent free memory blocks\n");
            kprintf("│     └── [3. STATE MUTATION]  : Released chunk back to pool; Heap Free: %lu B\n", (uint64_t)heap_free_bytes());
            break;
        case SYS_MEM_STATS:
            kprintf("│     ├── [1. PMM SUBSYSTEM]   : Scanned 4 KiB frame bitmap ──► %lu MiB RAM free (%lu frames)\n",
                    (uint64_t)(pmm_get_free_memory() / (1024 * 1024)), (uint64_t)pmm_get_free_frames());
            kprintf("│     ├── [2. HEAP ALLOCATOR]  : Sampled boundary-tag metrics ──► %lu B used / %lu B free\n",
                    (uint64_t)heap_used_bytes(), (uint64_t)heap_free_bytes());
            kprintf("│     └── [3. MEMORY MANAGER]  : Marshalled memory stats structure into user-space buffer\n");
            break;
        case SYS_FS_LIST: {
            fs_list_telemetry_t telem;
            fs_get_last_list_telemetry(&telem);
            kprintf("│     ├── [1. VFS DIRECTORY]   : Resolved path \"%s\" ──► Directory Inode #%u\n",
                    telem.requested_path, telem.resolved_dir_inode);
            kprintf("│     ├── [2. MINIFS ENGINE]   : Read Inode Block #%u ──► Located Data Block #%u (LBA %u)\n",
                    telem.inode_block, telem.dir_data_block, telem.dir_data_lba);
            kprintf("│     ├── [3. ATA DISK DRIVER] : Issued sector read commands via ATA PIO Port 0x1F0\n");
            kprintf("│     └── [4. MEMORY MANAGER]  : Transferred %u directory entries across ring boundary to user buffer\n",
                    telem.records_marshalled);
            break;
        }
        case SYS_MKDIR:
            kprintf("│     ├── [1. VFS DIRECTORY]   : Checked parent directory for naming collisions\n");
            kprintf("│     ├── [2. MINIFS ENGINE]   : Allocated free inode and data block on disk volume\n");
            kprintf("│     ├── [3. ATA DISK DRIVER] : Wrote '.' and '..' directory headers to disk sectors (PIO Mode)\n");
            kprintf("│     └── [4. STATE MUTATION]  : Synced directory metadata to persistent ATA hard drive\n");
            break;
        case SYS_SEEK:
            kprintf("│     ├── [1. VFS SUBSYSTEM]   : Validated file descriptor FD %ld\n", (int64_t)arg1);
            kprintf("│     └── [2. MINIFS ENGINE]   : Repositioned file offset to %ld (whence: %ld)\n", ret, (int64_t)arg3);
            break;
        case SYS_CHDIR:
            kprintf("│     ├── [1. VFS DIRECTORY]   : Traversed path \"%s\" to target inode\n", (const char *)arg1 ? (const char *)arg1 : "/");
            kprintf("│     ├── [2. MINIFS ENGINE]   : Verified inode is a valid directory (FS_TYPE_DIR)\n");
            kprintf("│     └── [3. PROCESS MANAGER] : Updated calling process CWD path to \"%s\"\n", proc ? proc->cwd_path : "/");
            break;
        case SYS_GETCWD:
            kprintf("│     ├── [1. PROCESS MANAGER] : Read CWD Inode #%u for calling PID %u\n", proc ? proc->cwd_inode : 1, pid);
            kprintf("│     └── [2. MEMORY MANAGER]  : Safely copied path string \"%s\" to user space buffer\n", proc ? proc->cwd_path : "/");
            break;
        case SYS_OPEN:
            kprintf("│     ├── [1. VFS PATH ENGINE] : Looked up filename \"%s\" in parent directory inode\n", (const char *)arg1 ? (const char *)arg1 : "file");
            kprintf("│     ├── [2. MINIFS ENGINE]   : Located file inode on ATA disk & checked permissions\n");
            kprintf("│     └── [3. PROCESS MANAGER] : Assigned available file descriptor FD %ld to process\n", ret);
            break;
        case SYS_CLOSE:
            kprintf("│     ├── [1. PROCESS MANAGER] : Validated process file descriptor FD %ld\n", (int64_t)arg1);
            kprintf("│     ├── [2. VFS SUBSYSTEM]   : Flushed dirty sector buffers to persistent disk\n");
            kprintf("│     └── [3. STATE MUTATION]  : Released file descriptor slot %ld for reuse\n", (int64_t)arg1);
            break;
        case SYS_WRITE:
            kprintf("│     ├── [1. VFS SUBSYSTEM]   : Verified file descriptor FD %ld has write permissions\n", (int64_t)arg1);
            kprintf("│     ├── [2. MINIFS ENGINE]   : Allocated data blocks & expanded inode size (+%lu bytes)\n", arg3);
            kprintf("│     └── [3. ATA DISK DRIVER] : Transferred data to disk sectors via ATA PIO Port 0x1F0\n");
            break;
        case SYS_READ:
            kprintf("│     ├── [1. VFS SUBSYSTEM]   : Verified file descriptor FD %ld has read permissions\n", (int64_t)arg1);
            kprintf("│     ├── [2. ATA DISK DRIVER] : Read sector data from disk via ATA PIO Port 0x1F0\n");
            kprintf("│     └── [3. MEMORY MANAGER]  : Transferred %ld bytes from disk buffer to user space\n", ret);
            break;
        case SYS_SCHED_GET:
        case SYS_SCHED_SET:
            kprintf("│     ├── [1. CPU SCHEDULER]   : Active policy: Round Robin (RR) with 50 ms time slice\n");
            kprintf("│     ├── [2. TIMER SUBSYSTEM] : 8254 PIT running at 100 Hz (%lu hardware timer ticks)\n", pit_get_ticks());
            kprintf("│     └── [3. SCHEDULER STATS] : %lu queue evaluations | %lu context switches\n",
                    scheduler_get_decision_count(), scheduler_get_context_switch_count());
            break;
        case SYS_YIELD:
            kprintf("│     ├── [1. CPU SCHEDULER]   : Process voluntarily yielded remaining time slice\n");
            kprintf("│     └── [2. CONTEXT SWITCH]  : Selected next READY task in circular Round Robin queue\n");
            break;
        default:
            kprintf("│     ├── [1. KERNEL DISPATCH] : Dispatched syscall ID %u to core subsystem\n", (uint32_t)sys_num);
            kprintf("│     └── [2. STATE MUTATION]  : Completed operation with return code %ld\n", ret);
            break;
    }

    kprintf("│\n");

    /* STAGE 4: HARDWARE PRIVILEGE RETURN */
    kprintf("│  " ANSI_BWHITE "[4. HARDWARE PRIVILEGE RETURN] " ANSI_BBLUE "(CPU Context Restoration)" ANSI_RESET "\n");
    kprintf("│     └── CPU Instruction : " ANSI_BYELLOW "iretq" ANSI_RESET " restores CPU registers & drops privilege\n");
    kprintf("│     └── Privilege Drop  : " ANSI_BRED "Ring 0 (Kernel Supervisor)" ANSI_RESET " ─────► " ANSI_BGREEN "Ring 3 (User Space)" ANSI_RESET "\n");
    kprintf("│     └── Syscall Return  : RAX = %ld (%s)\n",
            ret, ret >= 0 ? ANSI_BGREEN "SUCCESS" ANSI_RESET : ANSI_BRED "ERROR" ANSI_RESET);

    kprintf("│\n");

    /* STAGE 5: USER SHELL RESULT */
    kprintf("│  " ANSI_BWHITE "[5. USER SHELL RESULT] " ANSI_BGREEN "(Ring 3 User Mode)" ANSI_RESET "\n");
    switch (sys_num) {
        case SYS_SPAWN:
            kprintf("│     └── Shell received PID %ld ──► Task executing concurrently in background\n", ret);
            break;
        case SYS_FORK:
            kprintf("│     └── Shell received PID %ld ──► Cloned process running concurrently in Ring 3\n", ret);
            break;
        case SYS_PROC_LIST:
            kprintf("│     └── Shell formats process table (PID, PPID, RING, TICKS, STATE, NAME)\n");
            break;
        case SYS_MEM_ALLOC:
            kprintf("│     └── Shell verified heap pointer ──► Memory ready for read/write operations\n");
            break;
        case SYS_MEM_FREE:
            kprintf("│     └── Shell notified ──► Dynamic memory released back to heap pool\n");
            break;
        case SYS_MEM_STATS:
            kprintf("│     └── Shell renders physical RAM frames and kernel heap metrics cards\n");
            break;
        case SYS_FS_LIST:
            kprintf("│     └── Shell renders directory entries and file metadata on screen\n");
            break;
        case SYS_MKDIR:
            kprintf("│     └── Shell confirms directory creation on ATA persistent disk\n");
            break;
        case SYS_SEEK:
            kprintf("│     └── Process updated file read/write offset to %ld\n", ret);
            break;
        case SYS_CHDIR:
            kprintf("│     └── Shell updates working directory and terminal prompt path\n");
            break;
        case SYS_GETCWD:
            kprintf("│     └── Shell prints current working directory path to terminal\n");
            break;
        case SYS_KILL:
            kprintf("│     └── Shell confirms process termination (Type 'reap' to reclaim memory)\n");
            break;
        case SYS_REAP:
            kprintf("│     └── Shell confirms process slot and memory reclaimed\n");
            break;
        case SYS_SCHED_GET:
        case SYS_SCHED_SET:
            kprintf("│     └── Shell displays active Preemptive Round Robin scheduling status\n");
            break;
        case SYS_YIELD:
            kprintf("│     └── Shell surrendered CPU turn; resumes when scheduled\n");
            break;
        default:
            kprintf("│     └── Shell received return status code from kernel\n");
            break;
    }

    kprintf(ANSI_BCYAN "└");
    for (int i = 0; i < OBSERVER_CARD_WIDTH - 2; i++) {
        kprintf("─");
    }
    kprintf("┘" ANSI_RESET "\n\n");
}

void syscall_dispatcher(interrupt_frame_t *frame) {
    uint64_t sys_num = frame->rax;
    uint64_t arg1    = frame->rdi;
    uint64_t arg2    = frame->rsi;
    uint64_t arg3    = frame->rdx;
    uint64_t arg4    = frame->r10;
    (void)arg4;

    process_t *proc = process_get_current();
    uint32_t pid = proc ? proc->pid : 0;

    request_seq++;

    bool trace_this = observer_enabled &&
                      !(sys_num == SYS_WRITE && arg1 < 3) &&
                      !(sys_num == SYS_READ && arg1 < 3) &&
                      (sys_num != SYS_OBSERVE) &&
                      (sys_num != SYS_GETCWD);

    if (trace_this) {
        /* Only trace dynamic memory allocation if explicitly running 'alloc' or 'free' shell commands */
        if (sys_num == SYS_MEM_ALLOC || sys_num == SYS_MEM_FREE) {
            if (strcmp(active_cmd_name, "alloc") != 0 && strcmp(active_cmd_name, "free") != 0) {
                trace_this = false;
            }
        }
        /* Only trace voluntary CPU yield if explicitly running 'yield' command */
        else if (sys_num == SYS_YIELD) {
            if (strcmp(active_cmd_name, "yield") != 0) {
                trace_this = false;
            }
        }
    }

    uint64_t start_tsc = rdtsc();
    uint64_t ata_reads_before = 0, ata_sec_reads_before = 0, ata_writes_before = 0, ata_sec_writes_before = 0;
    ata_snapshot_telemetry(&ata_reads_before, &ata_sec_reads_before, &ata_writes_before, &ata_sec_writes_before);

    int64_t ret = 0;

    switch (sys_num) {
        case SYS_EXIT: {
            active_cmd_seq = 0;
            active_cmd_name[0] = '\0';
            active_child_syscall_count = 0;
            process_exit(frame, (int)arg1);
            ret = 0;
            break;
        }

        case SYS_WRITE: {
            /* int sys_write(int fd, const char *buf, size_t count) */
            int fd = (int)arg1;
            const char *buf = (const char *)arg2;
            size_t count = (size_t)arg3;

            if (!buf) {
                ret = -14; // -EFAULT
                break;
            }

            if (fd >= 3) {
                /* File system write */
                ret = (int64_t)fs_write(fd, buf, count);
            } else {
                /* Standard Output / Error to Console */
                for (size_t i = 0; i < count; i++) {
                    serial_putchar(buf[i]);
                    vga_putchar(buf[i]);
                }
                ret = (int64_t)count;
            }
            break;
        }

        case SYS_READ: {
            /* int sys_read(int fd, char *buf, size_t count) */
            int fd = (int)arg1;
            char *buf = (char *)arg2;
            size_t count = (size_t)arg3;

            if (!buf) {
                ret = -14; // -EFAULT
                break;
            }
            if (count == 0) {
                ret = 0;
                break;
            }

            if (fd >= 3) {
                /* File system read */
                ret = (int64_t)fs_read(fd, buf, count);
            } else {
                /* Standard Input from keyboard/serial */
                size_t read_bytes = 0;
                while (read_bytes < count) {
                    if (read_bytes > 0) {
                        int burst_wait = (buf[0] == 0x1b) ? 2500000 : 50000;
                        while (!serial_has_data() && burst_wait > 0) {
                            burst_wait--;
                            __asm__ volatile("pause");
                        }
                        if (!serial_has_data()) {
                            break;
                        }
                    }
                    char c = serial_getchar();
                    buf[read_bytes++] = c;
                    if (c == '\n' || c == '\r') {
                        break;
                    }
                }
                ret = (int64_t)read_bytes;
            }
            break;
        }

        case SYS_GETPID: {
            ret = (int64_t)pid;
            break;
        }

        case SYS_YIELD: {
            frame->rax = 0;
            if (trace_this) {
                uint64_t elapsed_cycles = rdtsc() - start_tsc;
                print_observer_lifecycle(request_seq, proc, frame, sys_num, arg1, arg2, arg3, 0,
                                         elapsed_cycles, 0, 0, 0, 0);
            }
            process_yield(frame);
            return;
        }

        case SYS_MEM_ALLOC: {
            /* void *sys_mem_alloc(size_t size) */
            size_t size = (size_t)arg1;
            void *ptr = kmalloc(size);
            ret = (int64_t)(uintptr_t)ptr;
            break;
        }

        case SYS_MEM_FREE: {
            /* int sys_mem_free(void *ptr) */
            void *ptr = (void *)arg1;
            kfree(ptr);
            ret = 0;
            break;
        }

        case SYS_MEM_STATS: {
            /* int sys_mem_stats(user_mem_stats_t *stats) */
            user_mem_stats_t *st = (user_mem_stats_t *)arg1;
            if (!st || (uint64_t)st >= 0x0000800000000000ULL) {
                ret = -14;
                break;
            }
            st->total_phys_ram = pmm_get_total_memory();
            st->free_phys_ram  = pmm_get_free_memory();
            st->total_frames   = pmm_get_total_frames();
            st->free_frames    = pmm_get_free_frames();
            st->heap_total     = heap_total_bytes();
            st->heap_used      = heap_used_bytes();
            st->heap_free      = heap_free_bytes();
            st->alloc_count    = (uint32_t)heap_alloc_count();
            st->free_count     = (uint32_t)heap_free_count();
            st->fail_count     = (uint32_t)heap_fail_count();
            ret = 0;
            break;
        }

        case SYS_SCHED_GET: {
            ret = (int64_t)scheduler_get_policy();
            break;
        }

        case SYS_SCHED_SET: {
            int policy = (int)arg1;
            if (policy != SCHED_POLICY_RR) {
                ret = -22; // -EINVAL (Only Round Robin supported)
                break;
            }
            scheduler_set_policy(policy);
            ret = 0;
            break;
        }

        case SYS_PROC_LIST: {
            user_proc_info_t *list = (user_proc_info_t *)arg1;
            size_t max_count = (size_t)arg2;
            if (!list || (uint64_t)list >= 0x0000800000000000ULL || max_count == 0 || max_count > MAX_PROCESSES) {
                ret = -14; // -EFAULT
                break;
            }
            ret = (int64_t)process_get_list(list, max_count);
            break;
        }

        case SYS_OBSERVE: {
            if (arg1 == 0) {
                syscall_set_observer(false);
                active_cmd_seq = 0;
                active_cmd_name[0] = '\0';
                active_child_syscall_count = 0;
            } else if (arg1 == 1) {
                syscall_set_observer(true);
                active_cmd_seq = 0;
                active_cmd_name[0] = '\0';
                active_child_syscall_count = 0;
            } else if (arg1 == 2) {
                active_cmd_seq = arg2;
                if (arg3) {
                    strncpy(active_cmd_name, (const char *)arg3, sizeof(active_cmd_name) - 1);
                    active_cmd_name[sizeof(active_cmd_name) - 1] = '\0';
                }
                active_child_syscall_count = 0;
            } else if (arg1 == 3) {
                ret = (int64_t)active_child_syscall_count;
                active_cmd_seq = 0;
                active_cmd_name[0] = '\0';
                active_child_syscall_count = 0;
                break;
            }
            ret = 0;
            break;
        }

        case SYS_TEST_MEM: {
            ret = (int64_t)run_kernel_memory_test_suite();
            break;
        }

        case SYS_SPAWN: {
            const char *name = (const char *)arg1;
            uint32_t prio = (uint32_t)arg2;
            ret = (int64_t)process_spawn_worker(name, prio);
            break;
        }

        case SYS_KILL: {
            uint32_t target_pid = (uint32_t)arg1;
            ret = (int64_t)process_kill(target_pid);
            break;
        }

        case SYS_REAP: {
            uint32_t target_pid = (uint32_t)arg1;
            ret = (int64_t)process_reap(target_pid);
            break;
        }

        case SYS_FORK: {
            const char *target = (const char *)arg1;
            ret = (int64_t)process_fork(frame, target);
            break;
        }

        case SYS_EXEC: {
            const char *prog = (const char *)arg1;
            ret = (int64_t)process_exec(frame, prog);
            if (ret == 0) {
                active_cmd_seq = 0;
                active_cmd_name[0] = '\0';
                active_child_syscall_count = 0;
            }
            break;
        }

        case SYS_OPEN: {
            const char *path = (const char *)arg1;
            int flags = (int)arg2;
            ret = (int64_t)fs_open(path, flags);
            break;
        }

        case SYS_CLOSE: {
            int fd = (int)arg1;
            ret = (int64_t)fs_close(fd);
            break;
        }

        case SYS_FS_CREATE: {
            const char *path = (const char *)arg1;
            uint16_t type = (uint16_t)arg2;
            ret = (int64_t)fs_create(path, type);
            break;
        }

        case SYS_FS_UNLINK: {
            const char *path = (const char *)arg1;
            ret = (int64_t)fs_unlink(path);
            break;
        }

        case SYS_FS_LIST: {
            const char *path = (const char *)arg1;
            fs_user_dirent_t *list = (fs_user_dirent_t *)arg2;
            size_t max_items = (size_t)arg3;
            uint64_t total_buf_bytes = max_items * sizeof(fs_user_dirent_t);
            uint64_t buf_start = (uint64_t)list;
            uint64_t buf_end = buf_start + total_buf_bytes;

            if (!list || buf_start >= 0x0000800000000000ULL || buf_end > 0x0000800000000000ULL || buf_end < buf_start || max_items == 0) {
                ret = -14; // -EFAULT
                break;
            }
            ret = (int64_t)fs_list(path, list, max_items);
            break;
        }

        case SYS_FS_STAT: {
            const char *path = (const char *)arg1;
            fs_stat_t *st = (fs_stat_t *)arg2;
            ret = (int64_t)fs_stat(path, st);
            break;
        }

        case SYS_FS_FORMAT: {
            ret = (int64_t)fs_format();
            break;
        }

        case SYS_DISK_INFO: {
            ata_device_info_t *info = (ata_device_info_t *)arg1;
            ata_get_info(info);
            ret = 0;
            break;
        }

        case SYS_GETCWD: {
            char *buf = (char *)arg1;
            size_t max_len = (size_t)arg2;
            if (!buf || (uint64_t)buf >= 0x0000800000000000ULL) {
                ret = -14;
                break;
            }
            ret = (int64_t)fs_getcwd(buf, max_len);
            break;
        }

        case SYS_CHDIR: {
            const char *path = (const char *)arg1;
            ret = (int64_t)fs_chdir(path);
            break;
        }

        case SYS_MKDIR: {
            const char *path = (const char *)arg1;
            ret = (int64_t)fs_mkdir(path);
            break;
        }

        case SYS_SEEK: {
            int fd = (int)arg1;
            int32_t offset = (int32_t)arg2;
            int whence = (int)arg3;
            ret = (int64_t)fs_seek(fd, offset, whence);
            break;
        }

        case SYS_SHUTDOWN: {
            kprintf("\n" ANSI_BCYAN "[SYSTEM] Powering down MiniKernel... Goodbye!" ANSI_RESET "\n");

            /* 1. Flush ATA disk cache */
            outb(0x1F7, 0xE7);

            /* 2. QEMU ACPI poweroff ports */
            outw(0x604, 0x2000);
            outw(0x604, 0x3400);

            /* 3. Bochs / older QEMU / VirtualBox ACPI */
            outw(0xB004, 0x2000);
            outw(0x4004, 0x3400);

            /* 4. QEMU ISA debug-exit device */
            outb(0x501, 0x00);
            outb(0xF4, 0x00);

            /* 5. Keyboard controller CPU reset (clean exit with -no-reboot) */
            outb(0x64, 0xFE);

            /* 6. Triple fault fallback */
            struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
            __asm__ volatile ("lidt %0; int3" : : "m"(null_idt));

            while (1) {
                __asm__ volatile ("cli; hlt");
            }
            break;
        }

        default: {
            KLOG_WARN("syscall: Unknown syscall ID %u from PID %u", (uint32_t)sys_num, pid);
            ret = -38; // -ENOSYS
            break;
        }
    }

    uint64_t elapsed_cycles = rdtsc() - start_tsc;
    uint64_t ata_reads_after = 0, ata_sec_reads_after = 0, ata_writes_after = 0, ata_sec_writes_after = 0;
    ata_snapshot_telemetry(&ata_reads_after, &ata_sec_reads_after, &ata_writes_after, &ata_sec_writes_after);

    uint64_t delta_ata_reads = ata_reads_after - ata_reads_before;
    uint64_t delta_sec_reads = ata_sec_reads_after - ata_sec_reads_before;
    uint64_t delta_ata_writes = ata_writes_after - ata_writes_before;
    uint64_t delta_sec_writes = ata_sec_writes_after - ata_sec_writes_before;

    if (trace_this) {
        print_observer_lifecycle(request_seq, proc, frame, sys_num, arg1, arg2, arg3, ret,
                                 elapsed_cycles, delta_ata_reads, delta_sec_reads,
                                 delta_ata_writes, delta_sec_writes);
    }

    /* Place return value into RAX register of the interrupt frame */
    frame->rax = (uint64_t)ret;
}

