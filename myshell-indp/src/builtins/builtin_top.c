#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "builtins.h"
#include "ui.h"
#include "syscall.h"

/*
 * Built-in: top
 * Live MiniKernel Process, Scheduler & Memory Monitor using int 0x80 syscalls:
 * - SYS_PROC_LIST  (E1 Process table & CPU ticks)
 * - SYS_SCHED_GET  (E1 Policy, quantum & context switch counters)
 * - SYS_MEM_STATS  (E2 Physical RAM frames & kernel heap usage)
 */
int builtin_top(int argc, char **argv, ShellState *state)
{
    (void)state;

    int max_iterations = 1;
    if (argc >= 3 && strcmp(argv[1], "-n") == 0) {
        max_iterations = atoi(argv[2]);
        if (max_iterations <= 0) {
            max_iterations = 1;
        }
    }

    const char *state_names[] = {"UNUSED", "EMBRYO", "READY", "RUNNING", "BLOCKED", "TERMINATED"};

    for (int iter = 0; iter < max_iterations; iter++) {
        if (max_iterations > 1) {
            printf("\033[H\033[2J");
        }

        int64_t policy  = sys_call(SYS_SCHED_GET, 0, 0, 0, 0);
        int64_t quantum = sys_call(SYS_SCHED_GET, 1, 0, 0, 0);
        int64_t csw     = sys_call(SYS_SCHED_GET, 2, 0, 0, 0);
        int64_t dec     = sys_call(SYS_SCHED_GET, 3, 0, 0, 0);

        const char *pol_str = "Round Robin (RR)";
        if (policy == SCHED_POLICY_PRIORITY) pol_str = "Priority Preemptive";
        else if (policy == SCHED_POLICY_FCFS) pol_str = "FCFS";

        user_mem_stats_t mem_st;
        memset(&mem_st, 0, sizeof(mem_st));
        sys_call(SYS_MEM_STATS, (uint64_t)(uintptr_t)&mem_st, 0, 0, 0);

        uint64_t total_mb = mem_st.total_phys_ram / (1024 * 1024);
        uint64_t free_mb  = mem_st.free_phys_ram / (1024 * 1024);
        uint64_t used_mb  = (total_mb >= free_mb) ? (total_mb - free_mb) : 0;

        user_proc_info_t procs[32];
        int64_t count = sys_call(SYS_PROC_LIST, (uint64_t)(uintptr_t)procs, 32, 0, 0);
        if (count < 0) count = 0;

        int running_cnt = 0, ready_cnt = 0, other_cnt = 0;
        for (int i = 0; i < count; i++) {
            if (procs[i].state == 3) running_cnt++;      /* PROC_RUNNING */
            else if (procs[i].state == 2) ready_cnt++;   /* PROC_READY */
            else other_cnt++;
        }

        printf("\n%sMiniKernel top (snapshot %d/%d)%s  Tasks: %ld total (%d running, %d ready, %d other)\n",
               COLOR_BOLD, iter + 1, max_iterations, COLOR_RESET,
               (long)count, running_cnt, ready_cnt, other_cnt);
        printf("Scheduler: %s%s%s | Quantum: %ld ticks (%ld ms) | Switches: %ld | Decisions: %ld\n",
               COLOR_BRIGHT_GREEN, pol_str, COLOR_RESET,
               (long)quantum, (long)quantum * 10, (long)csw, (long)dec);
        printf("Phys Mem : %s%lu MiB%s total, %s%lu MiB%s free, %s%lu MiB%s used | Heap: %lu KiB / %lu MiB\n",
               COLOR_BOLD, total_mb, COLOR_RESET,
               COLOR_BRIGHT_GREEN, free_mb, COLOR_RESET,
               COLOR_BRIGHT_YELLOW, used_mb, COLOR_RESET,
               mem_st.heap_used / 1024, mem_st.heap_total / (1024 * 1024));

        printf("\n%s\033[7m  PID   PPID  RING  PRIO  STATE        TICKS     COMMAND                    \033[0m\n", COLOR_BOLD);

        for (int i = 0; i < count; i++) {
            const char *st_name = (procs[i].state < 6) ? state_names[procs[i].state] : "UNKNOWN";
            const char *st_col  = (procs[i].state == 3) ? COLOR_BRIGHT_GREEN : COLOR_DIM;
            printf("  %-5u %-5u %-5u %-5u %s%-12s%s %-9lu %s\n",
                   procs[i].pid,
                   procs[i].ppid,
                   procs[i].ring,
                   procs[i].priority,
                   st_col, st_name, COLOR_RESET,
                   procs[i].cpu_ticks,
                   procs[i].name);
        }

        printf("\n%s[Direct MiniKernel Syscalls: SYS_PROC_LIST + SYS_MEM_STATS + SYS_SCHED_GET via int 0x80]%s\n\n",
               COLOR_DIM, COLOR_RESET);
        fflush(stdout);

        if (iter + 1 < max_iterations) {
            sys_call(SYS_YIELD, 0, 0, 0, 0);
        }
    }

    return 0;
}
