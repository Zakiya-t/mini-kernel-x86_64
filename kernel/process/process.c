#include "process/process.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/cpu.h"
#include "core/kprintf.h"
#include "fs/fs.h"
#include "exec/elf_loader.h"
#include "string.h"
#include "ansi_colors.h"

extern void enter_user_mode(uint64_t rip, uint64_t user_rsp, uint64_t rdi_arg, uint64_t rsi_arg);

static process_t process_table[MAX_PROCESSES];
static process_t *current_proc = NULL;
static uint32_t   next_pid = 1;

static int        current_policy = SCHED_POLICY_RR;
static uint64_t   stat_decisions = 0;
static uint64_t   stat_context_switches = 0;

void process_init(void) {
    for (size_t i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].pid   = 0;
        process_table[i].state = PROC_UNUSED;
    }

    /* Initialize Kernel Idle Process (PID 0) */
    process_t *idle = &process_table[0];
    idle->pid       = 0;
    idle->ppid      = 0;
    strcpy(idle->name, "kernel_idle");
    idle->state     = PROC_RUNNING;
    idle->priority  = 1;
    idle->cpu_ticks = 0;
    idle->time_slice = DEFAULT_QUANTUM;
    idle->ring      = 0;
    idle->pml4      = vmm_get_kernel_pml4();

    uint64_t idle_kstack = pmm_alloc_frames(4);
    idle->kernel_stack_top = idle_kstack + 4 * PAGE_SIZE;
    idle->user_stack_top   = 0;
    idle->cwd_inode        = 1;
    strcpy(idle->cwd_path, "/");
    gdt_set_kernel_stack(idle->kernel_stack_top);

    current_proc = idle;

    KLOG_OK("Process Manager initialized (PID 0: kernel_idle, Max slots: %u)", MAX_PROCESSES);
}

process_t *process_get_current(void) {
    return current_proc;
}

process_t *process_create(const char *name, void (*entry_point)(void), uint32_t priority, uint32_t ring) {
    process_t *p = NULL;
    for (size_t i = 1; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            p = &process_table[i];
            break;
        }
    }

    if (!p) {
        KLOG_ERR("process_create: Process table full (exceeded %u processes)!", MAX_PROCESSES);
        return NULL;
    }

    p->pid        = next_pid++;
    p->ppid       = current_proc ? current_proc->pid : 0;
    strncpy(p->name, name ? name : "process", sizeof(p->name) - 1);
    p->state      = PROC_EMBRYO;
    p->priority   = priority > 0 ? priority : 5;
    p->cpu_ticks  = 0;
    p->time_slice = DEFAULT_QUANTUM;
    p->ring       = ring;
    p->pml4       = vmm_get_kernel_pml4();
    p->cwd_inode  = current_proc ? current_proc->cwd_inode : 1;
    strcpy(p->cwd_path, (current_proc && current_proc->cwd_path[0]) ? current_proc->cwd_path : "/");

    /* Allocate dedicated 16 KiB Kernel Stack (for Ring 0 switches and interrupts) */
    uint64_t kstack_frame = pmm_alloc_frames(4);
    if (!kstack_frame) {
        p->state = PROC_UNUSED;
        return NULL;
    }
    p->kernel_stack_top = kstack_frame + 4 * PAGE_SIZE;

    /* Allocate dedicated 16 KiB User Stack if User Mode (Ring 3) */
    if (ring == 3) {
        uint64_t ustack_frame = pmm_alloc_frames(4);
        if (!ustack_frame) {
            pmm_free_frames(kstack_frame, 4);
            p->state = PROC_UNUSED;
            return NULL;
        }
        /* User stack mapped in canonical user range: 0x00007FFFF0000000 */
        uint64_t ustack_virt = 0x00007FFFF0000000ULL + (p->pid * 0x100000ULL);
        for (size_t f = 0; f < 4; f++) {
            vmm_map_page(p->pml4, ustack_virt + f * PAGE_SIZE, ustack_frame + f * PAGE_SIZE,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        p->user_stack_top = ustack_virt + 4 * PAGE_SIZE;
    } else {
        p->user_stack_top = 0;
    }

    memset(&p->context, 0, sizeof(p->context));
    p->context.rip    = (uint64_t)entry_point;
    p->context.rsp    = (ring == 3) ? (p->user_stack_top - 16) : (p->kernel_stack_top - 16);
    p->context.cs     = (ring == 3) ? (0x20 | 3) : 0x08;
    p->context.ss     = (ring == 3) ? (0x18 | 3) : 0x10;
    p->context.rflags = 0x202; // IF=1

    p->state = (entry_point != NULL) ? PROC_READY : PROC_EMBRYO;
    KLOG_OK("Process created: PID %u [\"%s\"] (Ring %u, User Stack: 0x%016lx)",
            p->pid, p->name, p->ring, p->user_stack_top);
    return p;
}

void process_enter_user_mode(process_t *proc) {
    if (!proc) return;

    /* Demote the previously executing process (e.g. PID 0 idle) to READY */
    if (current_proc && current_proc != proc && current_proc->state == PROC_RUNNING) {
        current_proc->state = PROC_READY;
    }

    current_proc = proc;
    proc->state = PROC_RUNNING;

    /* Set TSS.rsp0 to this process's dedicated kernel stack */
    gdt_set_kernel_stack(proc->kernel_stack_top);

    KLOG_INFO("Switching CPU privilege to Ring 3 (User Mode for PID %u: \"%s\")...",
              proc->pid, proc->name);
    enter_user_mode(proc->context.rip, proc->context.rsp, proc->context.rdi, proc->context.rsi);
}

void process_exit(interrupt_frame_t *frame, int code) {
    if (!current_proc || current_proc->pid == 0) return;

    if (code != 0) {
        KLOG_INFO("Process PID %u [\"%s\"] exited with code %d (State: TERMINATED)",
                  current_proc->pid, current_proc->name, code);
    }
    current_proc->state = PROC_TERMINATED;

    /* If PID 1 exited (e.g. replaced by exec), respawn /bin/myshell from disk! */
    if (current_proc->pid == 1) {
        if (code != 0) {
            KLOG_INFO("Init process terminated; reloading /bin/myshell from disk...");
        }
        process_t *sh = &process_table[1];
        strcpy(sh->name, "myshell");
        sh->state = PROC_RUNNING;
        sh->ring = 3;
        current_proc = sh;
        gdt_set_kernel_stack(sh->kernel_stack_top);
        const char *sh_argv[] = { "/bin/myshell", NULL };
        int res = elf_load("/bin/myshell", sh, &sh->context, 1, sh_argv);
        if (res == ELF_OK) {
            enter_user_mode(sh->context.rip, sh->context.rsp, sh->context.rdi, sh->context.rsi);
        } else {
            KLOG_ERR("PANIC: Failed to reload /bin/myshell: %s", elf_error_string(res));
            while (1) { hlt(); }
        }
    }

    process_yield(frame);
}

void scheduler_set_policy(int policy) {
    if (policy == SCHED_POLICY_RR) {
        current_policy = SCHED_POLICY_RR;
        KLOG_INFO("Scheduler policy confirmed: %s", scheduler_get_policy_name());
    } else {
        KLOG_WARN("scheduler_set_policy: Rejected policy %d (Only Round Robin supported)", policy);
    }
}

int scheduler_get_policy(void) {
    return SCHED_POLICY_RR;
}

const char *scheduler_get_policy_name(void) {
    return "Round Robin (RR)";
}

uint64_t scheduler_get_context_switch_count(void) {
    return stat_context_switches;
}

uint64_t scheduler_get_decision_count(void) {
    return stat_decisions;
}

static process_t *scheduler_pick_next(void) {
    stat_decisions++;

    /* Round Robin Scheduling (Default & Sole Policy) */
    size_t current_idx = current_proc ? (current_proc - process_table) : 0;
    for (size_t offset = 1; offset < MAX_PROCESSES; offset++) {
        size_t candidate = (current_idx + offset) % MAX_PROCESSES;
        if (candidate == 0) continue; // Skip idle if any other process ready
        if (process_table[candidate].state == PROC_READY || process_table[candidate].state == PROC_RUNNING) {
            return &process_table[candidate];
        }
    }

    return (current_proc && current_proc->state == PROC_RUNNING) ? current_proc : &process_table[0];
}

void scheduler_tick(interrupt_frame_t *frame) {
    if (!current_proc) return;

    current_proc->cpu_ticks++;

    if (current_proc->time_slice > 0) {
        current_proc->time_slice--;
    }

    if (current_proc->time_slice == 0) {
        current_proc->time_slice = DEFAULT_QUANTUM;

        process_t *next_proc = scheduler_pick_next();
        if (next_proc && next_proc != current_proc) {
            stat_context_switches++;

            /* Save current process state */
            current_proc->context = *frame;
            if (current_proc->state == PROC_RUNNING) {
                current_proc->state = PROC_READY;
            }

            /* Switch to next process */
            current_proc = next_proc;
            current_proc->state = PROC_RUNNING;
            gdt_set_kernel_stack(current_proc->kernel_stack_top);

            /* Restore context */
            *frame = current_proc->context;
        }
    }
}

void process_yield(interrupt_frame_t *frame) {
    process_t *next_proc = scheduler_pick_next();
    if (next_proc) {
        if (next_proc != current_proc || current_proc->state == PROC_TERMINATED) {
            stat_context_switches++;
            if (frame && current_proc->state == PROC_RUNNING) {
                current_proc->context = *frame;
                current_proc->state = PROC_READY;
            }
            current_proc = next_proc;
            current_proc->state = PROC_RUNNING;
            gdt_set_kernel_stack(current_proc->kernel_stack_top);
            if (frame) {
                *frame = current_proc->context;
            }
        }
    }
}

/* Educational background worker task running in Ring 3 */
void user_worker_task(void) {
    while (1) {
        for (volatile uint64_t i = 0; i < 10000000; i++) {}
    }
}

int process_spawn_worker(const char *name, uint32_t priority) {
    if (!name || name[0] == '\0') {
        name = "worker";
    }
    if (priority == 0) priority = 5;
    if (priority > 10) priority = 10;

    /* 1. Check if 'name' refers to an executable ELF file stored on MiniFS disk */
    char candidate[128];
    const char *search_prefixes[] = { "", "/bin/", "/" };
    fs_stat_t st;
    bool found = false;

    for (size_t i = 0; i < 3; i++) {
        if (search_prefixes[i][0] == '\0') {
            strncpy(candidate, name, sizeof(candidate) - 1);
        } else {
            if (name[0] == '/') continue;
            candidate[0] = '\0';
            strncat(candidate, search_prefixes[i], sizeof(candidate) - 1);
            strncat(candidate, name, sizeof(candidate) - strlen(candidate) - 1);
        }
        candidate[sizeof(candidate) - 1] = '\0';

        if (fs_stat(candidate, &st) == 0 && st.type == FS_TYPE_FILE) {
            found = true;
            break;
        }
    }

    if (found) {
        /* Extract binary basename for process name */
        const char *bname = candidate;
        for (const char *p = candidate; *p; p++) {
            if (*p == '/') bname = p + 1;
        }

        process_t *p = process_create(bname, NULL, priority, 3);
        if (!p) {
            return -1;
        }

        int res = elf_load(candidate, p, &p->context, 0, NULL);
        if (res != ELF_OK) {
            KLOG_ERR("process_spawn: ELF load of \"%s\" failed: %s",
                     candidate, elf_error_string(res));
            p->state = PROC_UNUSED;
            return -2;
        }

        p->state = PROC_READY;
        KLOG_OK("process_spawn: Program \"%s\" loaded from disk into Process PID %u [\"%s\"] (Ring 3)",
                candidate, p->pid, p->name);
        return (int)p->pid;
    }

    /* 2. Fallback: Internal educational background worker task */
    process_t *p = process_create(name, user_worker_task, priority, 3);
    if (!p) {
        return -1;
    }
    return (int)p->pid;
}

int process_kill(uint32_t pid) {
    if (pid == 0 || pid == 1) {
        return -1; /* Protection: Cannot kill idle or shell */
    }

    for (size_t i = 2; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROC_UNUSED) {
            if (process_table[i].state == PROC_TERMINATED) {
                return -2; /* Already terminated */
            }
            process_table[i].state = PROC_TERMINATED;
            KLOG_INFO("Process PID %u [\"%s\"] terminated via kill request",
                      pid, process_table[i].name);
            return 0;
        }
    }
    return -3; /* Not found */
}

int process_reap(uint32_t pid) {
    if (pid == 0 || pid == 1) {
        return -1; /* Protection: Cannot reap idle or shell */
    }

    for (size_t i = 2; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid && process_table[i].state != PROC_UNUSED) {
            if (process_table[i].state != PROC_TERMINATED) {
                return -2; /* Process is still active, must kill first */
            }

            /* 1. Free kernel stack frames */
            if (process_table[i].kernel_stack_top) {
                uint64_t kstack_frame = process_table[i].kernel_stack_top - 4 * PAGE_SIZE;
                pmm_free_frames(kstack_frame, 4);
            }

            /* 2. Free user stack frames and unmap */
            if (process_table[i].ring == 3 && process_table[i].user_stack_top) {
                uint64_t ustack_virt = process_table[i].user_stack_top - 4 * PAGE_SIZE;
                for (size_t f = 0; f < 4; f++) {
                    uint64_t phys = vmm_virt_to_phys(process_table[i].pml4, ustack_virt + f * PAGE_SIZE);
                    if (phys) {
                        pmm_free_frame(phys);
                    }
                    vmm_unmap_page(process_table[i].pml4, ustack_virt + f * PAGE_SIZE);
                }
            }

            KLOG_INFO("Process PID %u [\"%s\"] reaped (PCB slot and memory reclaimed)",
                      pid, process_table[i].name);

            /* 3. Reset PCB slot back to PROC_UNUSED */
            process_table[i].pid              = 0;
            process_table[i].ppid             = 0;
            process_table[i].name[0]          = '\0';
            process_table[i].state            = PROC_UNUSED;
            process_table[i].priority         = 0;
            process_table[i].cpu_ticks        = 0;
            process_table[i].time_slice       = 0;
            process_table[i].kernel_stack_top = 0;
            process_table[i].user_stack_top   = 0;

            return 0;
        }
    }

    return -3; /* Not found */
}

int process_fork(interrupt_frame_t *parent_frame, const char *target) {
    if (!current_proc || !parent_frame) return -1;

    process_t *src = NULL;
    if (!target || target[0] == '\0' || strcmp(target, "self") == 0 || strcmp(target, "shell") == 0) {
        src = current_proc;
    } else {
        /* Check if target is a numeric PID */
        bool is_num = true;
        for (const char *s = target; *s; s++) {
            if (*s < '0' || *s > '9') {
                is_num = false;
                break;
            }
        }

        if (is_num) {
            uint32_t t_pid = 0;
            for (const char *s = target; *s >= '0' && *s <= '9'; s++) {
                t_pid = t_pid * 10 + (*s - '0');
            }
            for (size_t i = 1; i < MAX_PROCESSES; i++) {
                if (process_table[i].pid == t_pid && process_table[i].state != PROC_UNUSED) {
                    src = &process_table[i];
                    break;
                }
            }
        } else {
            /* Match by name */
            for (size_t i = 1; i < MAX_PROCESSES; i++) {
                if (process_table[i].state != PROC_UNUSED && strcmp(process_table[i].name, target) == 0) {
                    src = &process_table[i];
                    break;
                }
            }
        }

        if (!src) {
            return -2; /* Target not found */
        }
        if (src->state == PROC_TERMINATED) {
            return -3; /* Target is terminated */
        }
    }

    /* 1. Find unused PCB slot */
    process_t *child = NULL;
    for (size_t i = 1; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            child = &process_table[i];
            break;
        }
    }
    if (!child) {
        KLOG_ERR("fork: Process table full (exceeded %u processes)!", MAX_PROCESSES);
        return -1;
    }

    /* 2. Assign PID and parent metadata */
    child->pid        = next_pid++;
    child->ppid       = src->pid;
    strncpy(child->name, src->name, sizeof(child->name) - 1);
    child->name[sizeof(child->name) - 1] = '\0';
    child->state      = PROC_EMBRYO;
    child->priority   = src->priority;
    child->cpu_ticks  = 0;
    child->time_slice = DEFAULT_QUANTUM;
    child->ring       = src->ring;
    child->pml4       = src->pml4;
    child->cwd_inode  = src->cwd_inode;
    strncpy(child->cwd_path, src->cwd_path, sizeof(child->cwd_path) - 1);
    child->cwd_path[sizeof(child->cwd_path) - 1] = '\0';

    /* 3. Allocate dedicated 16 KiB Kernel Stack for child */
    uint64_t kstack_frame = pmm_alloc_frames(4);
    if (!kstack_frame) {
        child->state = PROC_UNUSED;
        return -1;
    }
    child->kernel_stack_top = kstack_frame + 4 * PAGE_SIZE;

    /* 4. Allocate and map dedicated 16 KiB User Stack for child (if Ring 3) */
    if (src->ring == 3) {
        uint64_t ustack_frame = pmm_alloc_frames(4);
        if (!ustack_frame) {
            pmm_free_frames(kstack_frame, 4);
            child->state = PROC_UNUSED;
            return -1;
        }

        uint64_t child_ustack_virt = 0x00007FFFF0000000ULL + (child->pid * 0x100000ULL);
        for (size_t f = 0; f < 4; f++) {
            vmm_map_page(child->pml4, child_ustack_virt + f * PAGE_SIZE,
                         ustack_frame + f * PAGE_SIZE,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
        }
        child->user_stack_top = child_ustack_virt + 4 * PAGE_SIZE;

        /* Duplicate src's user stack into child */
        uint64_t src_ustack_base = src->user_stack_top - 4 * PAGE_SIZE;
        uint64_t child_ustack_base = child->user_stack_top - 4 * PAGE_SIZE;
        memcpy((void *)child_ustack_base, (void *)src_ustack_base, 4 * PAGE_SIZE);

        int64_t stack_delta = (int64_t)child->user_stack_top - (int64_t)src->user_stack_top;

        if (src == current_proc) {
            child->context = *parent_frame;
            child->context.rsp = parent_frame->rsp + stack_delta;
            if (parent_frame->rbp >= src_ustack_base && parent_frame->rbp < src->user_stack_top) {
                child->context.rbp = parent_frame->rbp + stack_delta;
            } else {
                child->context.rbp = parent_frame->rbp;
            }
            child->context.rax = 0; /* Child returns 0 */
        } else {
            child->context = src->context;
            child->context.rsp = src->context.rsp + stack_delta;
            if (src->context.rbp >= src_ustack_base && src->context.rbp < src->user_stack_top) {
                child->context.rbp = src->context.rbp + stack_delta;
            } else {
                child->context.rbp = src->context.rbp;
            }
            child->context.rax = 0;
        }
    } else {
        child->user_stack_top = 0;
        child->context = (src == current_proc) ? *parent_frame : src->context;
        int64_t stack_delta = (int64_t)child->kernel_stack_top - (int64_t)src->kernel_stack_top;
        child->context.rsp = ((src == current_proc) ? parent_frame->rsp : src->context.rsp) + stack_delta;
        child->context.rbp = ((src == current_proc) ? parent_frame->rbp : src->context.rbp) + stack_delta;
        child->context.rax = 0;
    }

    /* 6. Mark child as READY in scheduler queue */
    child->state = PROC_READY;

    KLOG_OK("Process forked: Source PID %u [\"%s\"] -> Clone PID %u [\"%s\"] (User Stack: 0x%016lx)",
            src->pid, src->name, child->pid, child->name, child->user_stack_top);

    /* Return child's PID to caller */
    return (int)child->pid;
}

int process_exec(interrupt_frame_t *frame, const char *prog_name) {
    if (!current_proc || !frame) return -1;
    if (!prog_name || prog_name[0] == '\0') return -2;

    /* Make local copy of command line to tokenize */
    char kcmd[256];
    strncpy(kcmd, prog_name, sizeof(kcmd) - 1);
    kcmd[sizeof(kcmd) - 1] = '\0';

    /* Parse into argc and argv (supporting quoted arguments) */
    char *argv[32];
    int argc = 0;
    char *p = kcmd;

    while (*p && argc < 31) {
        while (*p == ' ') p++;
        if (*p == '\0') break;

        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            argv[argc++] = p;
            while (*p && *p != quote) p++;
            if (*p == quote) {
                *p++ = '\0';
            }
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) {
                *p++ = '\0';
            }
        }
    }
    argv[argc] = NULL;

    if (argc == 0) return -2;

    const char *prog = argv[0];

    /* Legacy named-task fallback (worker) */
    void (*entry)(void) = NULL;
    if (strcmp(prog, "z1") == 0 || strcmp(prog, "worker") == 0) {
        entry = user_worker_task;
    }

    if (entry) {
        strncpy(current_proc->name, prog, sizeof(current_proc->name) - 1);
        current_proc->name[sizeof(current_proc->name) - 1] = '\0';

        frame->rsp    = current_proc->user_stack_top - 16;
        frame->rbp    = frame->rsp;
        frame->rip    = (uint64_t)entry;
        frame->rax    = 0; frame->rbx = 0; frame->rcx = 0; frame->rdx = 0;
        frame->rsi    = 0; frame->rdi = 0;
        frame->r8     = 0; frame->r9  = 0; frame->r10 = 0; frame->r11 = 0;
        frame->r12    = 0; frame->r13 = 0; frame->r14 = 0; frame->r15 = 0;
        frame->cs     = (0x20 | 3);
        frame->ss     = (0x18 | 3);
        frame->rflags = 0x202;
        return 0;
    }

    /* Search paths for ELF binary:
     * 1. prog itself (e.g. "/bin/tcc" or "test_libc")
     * 2. "/bin/" + prog (e.g. "/bin/tcc")
     * 3. "/" + prog (e.g. "/test_libc")
     */
    char candidate[128];
    const char *search_prefixes[] = { "", "/bin/", "/" };
    fs_stat_t st;
    bool found = false;

    for (size_t i = 0; i < 3; i++) {
        if (search_prefixes[i][0] == '\0') {
            strncpy(candidate, prog, sizeof(candidate) - 1);
        } else {
            if (prog[0] == '/') continue;
            candidate[0] = '\0'; strncat(candidate, search_prefixes[i], sizeof(candidate) - 1); strncat(candidate, prog, sizeof(candidate) - strlen(candidate) - 1);
        }
        candidate[sizeof(candidate) - 1] = '\0';

        if (fs_stat(candidate, &st) == 0 && st.type == FS_TYPE_FILE) {
            found = true;
            break;
        }
    }

    if (!found) {
        KLOG_ERR("process_exec: Executable \"%s\" not found on MiniFS disk", prog);
        return -2;
    }

    int res = elf_load(candidate, current_proc, frame, argc, (const char *const *)argv);
    if (res == ELF_OK) {
        // KLOG_OK("process_exec: ELF binary \"%s\" (argc=%d) loaded via ELF Loader", candidate, argc);
        return 0;
    }

    KLOG_ERR("process_exec: ELF load of \"%s\" failed: %s", candidate, elf_error_string(res));
    return res;
}
size_t process_get_list(user_proc_info_t *list, size_t max_count) {
    if (!list || max_count == 0) return 0;

    size_t count = 0;
    for (size_t i = 0; i < MAX_PROCESSES && count < max_count; i++) {
        if (process_table[i].state != PROC_UNUSED) {
            list[count].pid       = process_table[i].pid;
            list[count].ppid      = process_table[i].ppid;
            strncpy(list[count].name, process_table[i].name, sizeof(list[count].name) - 1);
            list[count].name[sizeof(list[count].name) - 1] = '\0';
            list[count].state     = (uint32_t)process_table[i].state;
            list[count].priority  = process_table[i].priority;
            list[count].cpu_ticks = process_table[i].cpu_ticks;
            list[count].ring      = process_table[i].ring;
            count++;
        }
    }
    return count;
}
