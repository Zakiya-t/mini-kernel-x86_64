#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include "types.h"
#include "arch/x86_64/idt.h"
#include "syscall_nums.h"

#define MAX_PROCESSES      64
#define DEFAULT_QUANTUM    5   /* 5 timer ticks = 50ms at 100 Hz */

typedef enum {
    PROC_UNUSED     = 0,
    PROC_EMBRYO     = 1,
    PROC_READY      = 2,
    PROC_RUNNING    = 3,
    PROC_BLOCKED    = 4,
    PROC_TERMINATED = 5
} process_state_t;

typedef struct process {
    uint32_t        pid;
    uint32_t        ppid;
    char            name[32];
    process_state_t state;
    uint32_t        priority;      /* 1 (lowest) to 10 (highest) */
    uint64_t        cpu_ticks;     /* Process CPU runtime accounting */
    uint64_t        time_slice;    /* Remaining quantum ticks */
    uint32_t        ring;          /* 0 = Kernel, 3 = User */

    uint64_t       *pml4;          /* 4-level page table */
    uint64_t        kernel_stack_top;
    uint64_t        user_stack_top;

    uint32_t        cwd_inode;     /* Current working directory inode */
    char            cwd_path[128]; /* Canonical working directory path string */

    interrupt_frame_t context;     /* Saved CPU context */
    struct process *next;
} process_t;

void       process_init(void);
process_t *process_create(const char *name, void (*entry_point)(void), uint32_t priority, uint32_t ring);
process_t *process_get_current(void);
void       process_exit(interrupt_frame_t *frame, int code);
void       process_yield(interrupt_frame_t *frame);
int        process_spawn_worker(const char *name, uint32_t priority);
int        process_kill(uint32_t pid);
int        process_reap(uint32_t pid);
int        process_fork(interrupt_frame_t *parent_frame, const char *target);
int        process_exec(interrupt_frame_t *frame, const char *prog_name);

/* Scheduler */
void       scheduler_tick(interrupt_frame_t *frame);
void       scheduler_set_policy(int policy);
int        scheduler_get_policy(void);
const char*scheduler_get_policy_name(void);
uint64_t   scheduler_get_context_switch_count(void);
uint64_t   scheduler_get_decision_count(void);

size_t     process_get_list(user_proc_info_t *list, size_t max_count);

/* Ring 3 transition */
void       process_enter_user_mode(process_t *proc);

#endif /* _KERNEL_PROCESS_H */
