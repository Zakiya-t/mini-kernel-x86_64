#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include "builtins.h"
#include "ui.h"
#include "syscall.h"

int builtin_sysinfo(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;

    struct utsname un;
    memset(&un, 0, sizeof(un));
    uname(&un);

    /* Query calling process ID and x86-64 hardware privilege ring */
    uint16_t cs = 0;
    __asm__ volatile("mov %%cs, %0" : "=r"(cs));
    int ring = cs & 3;
    int64_t pid = sys_call(SYS_GETPID, 0, 0, 0, 0);

    /* Query live Physical Memory & Kernel Heap telemetry via int 0x80 */
    user_mem_stats_t mem_st;
    memset(&mem_st, 0, sizeof(mem_st));
    sys_call(SYS_MEM_STATS, (uint64_t)(uintptr_t)&mem_st, 0, 0, 0);

    uint64_t total_mb = mem_st.total_phys_ram / (1024 * 1024);
    uint64_t free_mb  = mem_st.free_phys_ram / (1024 * 1024);
    uint64_t used_mb  = (total_mb >= free_mb) ? (total_mb - free_mb) : 0;

    /* Query active CPU scheduler metrics via int 0x80 */
    int64_t policy  = sys_call(SYS_SCHED_GET, 0, 0, 0, 0);
    int64_t quantum = sys_call(SYS_SCHED_GET, 1, 0, 0, 0);
    int64_t csw     = sys_call(SYS_SCHED_GET, 2, 0, 0, 0);

    const char *pol_str = "Round Robin (RR)";
    if (policy == SCHED_POLICY_PRIORITY) pol_str = "Priority Preemptive";
    else if (policy == SCHED_POLICY_FCFS) pol_str = "First-Come First-Served (FCFS)";

    /* Query active process count via int 0x80 */
    user_proc_info_t procs[32];
    int64_t proc_count = sys_call(SYS_PROC_LIST, (uint64_t)(uintptr_t)procs, 32, 0, 0);
    if (proc_count < 0) proc_count = 0;

    /* Query primary ATA storage device via int 0x80 */
    user_ata_device_info_t disk;
    memset(&disk, 0, sizeof(disk));
    sys_call(SYS_DISK_INFO, (uint64_t)(uintptr_t)&disk, 0, 0, 0);

    printf("\n");
    printf("%s----------------------------------------------------------------------%s\n", COLOR_CYAN, COLOR_RESET);
    printf("%s  MiniKernel x86-64 System Information Dashboard%s\n", COLOR_BOLD, COLOR_RESET);
    printf("%s----------------------------------------------------------------------%s\n", COLOR_CYAN, COLOR_RESET);
    printf("  %sUser / Host:%s       %sroot%s @ %s%s%s (PID %ld, Ring %d)\n",
           COLOR_BOLD, COLOR_RESET,
           COLOR_BRIGHT_GREEN, COLOR_RESET,
           COLOR_BRIGHT_BLUE, un.nodename[0] ? un.nodename : "minikernel", COLOR_RESET,
           (long)pid, ring);
    printf("  %sOS:%s                %s %s (%s)\n",
           COLOR_BOLD, COLOR_RESET,
           un.sysname[0] ? un.sysname : "MiniKernel",
           un.release[0] ? un.release : "1.0.0",
           un.version[0] ? un.version : "x86-64 Ring 0 Kernel");
    printf("  %sArchitecture:%s      %s (4-Level PML4 Paging)\n",
           COLOR_BOLD, COLOR_RESET, un.machine[0] ? un.machine : "x86_64");
    printf("  %sSyscall ABI:%s       int 0x80 (31 Native Kernel Services)\n", COLOR_BOLD, COLOR_RESET);
    printf("  %sCPU Scheduler:%s     %s (Quantum: %ld ticks, Switches: %ld)\n",
           COLOR_BOLD, COLOR_RESET, pol_str, (long)quantum, (long)csw);
    printf("  %sActive Tasks:%s      %ld processes in PCB table\n",
           COLOR_BOLD, COLOR_RESET, (long)proc_count);
    printf("  %sPhysical Memory:%s   %lu MiB used / %lu MiB total (%lu free frames)\n",
           COLOR_BOLD, COLOR_RESET, used_mb, total_mb, mem_st.free_frames);
    printf("  %sKernel Heap:%s       %lu KiB used / %lu MiB total\n",
           COLOR_BOLD, COLOR_RESET, mem_st.heap_used / 1024, mem_st.heap_total / (1024 * 1024));
    if (disk.present) {
        printf("  %sATA Storage:%s       %s (%u MiB, MiniFS v2 @ LBA 2048)\n",
               COLOR_BOLD, COLOR_RESET, disk.model, disk.size_mb);
    }
    printf("  %sShell:%s             %s %s (Independent Ring 3 ELF64)\n",
           COLOR_BOLD, COLOR_RESET, SHELL_NAME, SHELL_VERSION);
    printf("  %sActive Directory:%s  %s\n", COLOR_BOLD, COLOR_RESET, state->current_dir);
    printf("  %sSession History:%s   %d commands recorded\n",
           COLOR_BOLD, COLOR_RESET, state->history_count);
    printf("%s----------------------------------------------------------------------%s\n\n", COLOR_CYAN, COLOR_RESET);

    return 0;
}
