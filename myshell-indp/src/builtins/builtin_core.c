#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "builtins.h"
#include "ui.h"
#include "syscall.h"

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

static void set_env_var(const char *key, const char *value)
{
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

int builtin_cd(int argc, char **argv, ShellState *state)
{
    const char *target = NULL;

    if (argc < 2 || strcmp(argv[1], "~") == 0) {
        target = getenv("HOME");
#ifdef _WIN32
        if (!target)
            target = getenv("USERPROFILE");
#endif
        if (!target) {
            fprintf(stderr, "myshell: cd: HOME not set\n");
            return 1;
        }
    } else if (strcmp(argv[1], "-") == 0) {
        if (state->prev_dir[0] == '\0') {
            fprintf(stderr, "myshell: cd: OLDPWD not set\n");
            return 1;
        }
        target = state->prev_dir;
        printf("%s\n", target);
    } else {
        target = argv[1];
    }

    /* Save current directory as previous before changing */
    char temp_dir[1024];
    if (getcwd(temp_dir, sizeof(temp_dir)) != NULL) {
        strncpy(state->prev_dir, temp_dir, sizeof(state->prev_dir) - 1);
        state->prev_dir[sizeof(state->prev_dir) - 1] = '\0';
        set_env_var("OLDPWD", state->prev_dir);
    }

    if (chdir(target) != 0) {
        fprintf(stderr, "myshell: cd: %s: %s\n", target, strerror(errno));
        return 1;
    }

    shell_state_update_cwd(state);
    set_env_var("PWD", state->current_dir);
    return 0;
}

int builtin_pwd(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    printf("%s\n", state->current_dir);
    return 0;
}

int builtin_echo(int argc, char **argv, ShellState *state)
{
    (void)state;
    int start = 1;
    bool newline = true;

    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        newline = false;
        start = 2;
    }

    for (int i = start; i < argc; i++) {
        printf("%s%s", argv[i], (i + 1 < argc) ? " " : "");
    }

    if (newline)
        putchar('\n');
    else
        fflush(stdout);

    return 0;
}

int builtin_clear(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    (void)state;
    printf("\033[H\033[2J\033[3J");
    fflush(stdout);
    return 0;
}

int builtin_history(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < state->history_count; i++) {
        printf("%s%5d%s  %s\n", COLOR_DIM, i + 1, COLOR_RESET, state->history[i]);
    }
    return 0;
}

int builtin_type(int argc, char **argv, ShellState *state)
{
    (void)state;
    if (argc < 2) {
        fprintf(stderr, "myshell: type: missing operand\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (is_builtin(argv[i])) {
            printf("%s is a %sMyShell built-in%s command\n", argv[i], COLOR_GREEN, COLOR_RESET);
        } else {
            printf("%s is an %sexternal system command%s (resolved via PATH)\n", argv[i], COLOR_CYAN, COLOR_RESET);
        }
    }
    return 0;
}

int builtin_ps(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    (void)state;
    user_proc_info_t list[32];
    int64_t count = sys_call(SYS_PROC_LIST, (uint64_t)(uintptr_t)list, 32, 0, 0);
    if (count <= 0) {
        printf("No active processes found.\n");
        return 0;
    }
    printf("\n%s  PID   PPID  STATE       RING  TICKS     NAME%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  --------------------------------------------------------\n");
    const char *state_names[] = {"UNUSED", "EMBRYO", "READY", "RUNNING", "BLOCKED", "TERMINATED"};
    for (int i = 0; i < count; i++) {
        const char *st_name = (list[i].state < 6) ? state_names[list[i].state] : "UNKNOWN";
        printf("  %-5u %-5u %-11s %-5u %-9lu %s\n",
               list[i].pid, list[i].ppid, st_name, list[i].ring, list[i].cpu_ticks, list[i].name);
    }
    printf("\n");
    return 0;
}

int builtin_mem(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    (void)state;
    user_mem_stats_t st;
    memset(&st, 0, sizeof(st));
    int64_t ret = sys_call(SYS_MEM_STATS, (uint64_t)(uintptr_t)&st, 0, 0, 0);
    if (ret < 0) {
        printf("Failed to query memory statistics.\n");
        return 1;
    }
    printf("\n%sMiniKernel Memory Telemetry:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  Total Physical RAM : %lu MiB (%lu frames)\n", st.total_phys_ram / (1024 * 1024), st.total_frames);
    printf("  Free Physical RAM  : %lu MiB (%lu frames)\n", st.free_phys_ram / (1024 * 1024), st.free_frames);
    printf("  Kernel Heap Total  : %lu MiB\n", st.heap_total / (1024 * 1024));
    printf("  Kernel Heap Used   : %lu KiB\n", st.heap_used / 1024);
    printf("  Kernel Heap Free   : %lu KiB\n\n", st.heap_free / 1024);
    return 0;
}

int builtin_spawn(int argc, char **argv, ShellState *state)
{
    (void)state;
    const char *name = (argc > 1) ? argv[1] : "worker";
    int64_t pid = sys_call(SYS_SPAWN, (uint64_t)name, 5, 0, 0);
    if (pid > 0) {
        printf("Spawned background worker \"%s\" (PID %ld)\n", name, pid);
        return 0;
    }
    printf("Failed to spawn process \"%s\"\n", name);
    return 1;
}

int builtin_kill(int argc, char **argv, ShellState *state)
{
    (void)state;
    if (argc < 2) {
        printf("Usage: kill <pid>\n");
        return 1;
    }
    uint32_t pid = (uint32_t)atoi(argv[1]);
    int64_t ret = sys_call(SYS_KILL, (uint64_t)pid, 0, 0, 0);
    if (ret == 0) {
        printf("Process PID %u terminated.\n", pid);
        return 0;
    }
    printf("Failed to kill PID %u\n", pid);
    return 1;
}

int builtin_write(int argc, char **argv, ShellState *state)
{
    (void)state;
    if (argc < 3) {
        printf("Usage: write <file> <content>\n");
        return 1;
    }
    FILE *f = fopen(argv[1], "w");
    if (!f) {
        printf("write: cannot open '%s'\n", argv[1]);
        return 1;
    }
    fputs(argv[2], f);
    fputs("\n", f);
    fclose(f);
    printf("Wrote %lu bytes to %s\n", (unsigned long)strlen(argv[2]) + 1, argv[1]);
    return 0;
}

int builtin_shutdown(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    state->is_running = false;
    printf("\nShutting down MiniKernel...\n");
    sys_call(SYS_SHUTDOWN, 0, 0, 0, 0);
    return 0;
}

int builtin_observe(int argc, char **argv, ShellState *state)
{
    if (argc < 2) {
        printf("Usage: observe on | observe off\n");
        printf("Live Kernel Lifecycle Observer is currently: %s\n",
               state->observer_enabled ? "\033[1;32mENABLED\033[0m" : "\033[1;31mDISABLED\033[0m");
        return 1;
    }
    if (strcmp(argv[1], "on") == 0) {
        sys_call(SYS_OBSERVE, 1, 0, 0, 0);
        state->observer_enabled = true;
        printf("Live Kernel Lifecycle Observer: \033[1;32mENABLED\033[0m\n");
    } else if (strcmp(argv[1], "off") == 0) {
        sys_call(SYS_OBSERVE, 0, 0, 0, 0);
        state->observer_enabled = false;
        printf("Live Kernel Lifecycle Observer: \033[1;31mDISABLED\033[0m\n");
    } else {
        printf("Usage: observe on | observe off\n");
        return 1;
    }
    return 0;
}

int builtin_exit(int argc, char **argv, ShellState *state)
{
    state->is_running = false;
    if (argc > 1) {
        state->last_exit_code = atoi(argv[1]);
    }
    printf("\nExiting shell, shutting down MiniKernel...\n");
    sys_call(SYS_SHUTDOWN, 0, 0, 0, 0);
    return state->last_exit_code;
}

/* Builtins registry table */
static const BuiltinCommand BUILTINS[] = {
<<<<<<< HEAD
    {"cd",       builtin_cd,       "Change the current directory",          "cd [dir | ~ | -]"},
    {"pwd",      builtin_pwd,      "Print current working directory",       "pwd"},
    {"ls",       builtin_ls,       "List directory contents with colors",   "ls [-l] [-a] [path]"},
    {"cat",      builtin_cat,      "Concatenate and display files",         "cat [-n] <file...>"},
    {"mkdir",    builtin_mkdir,    "Create directory",                      "mkdir <dir...>"},
    {"touch",    builtin_touch,    "Create empty file or update timestamp", "touch <file...>"},
    {"write",    builtin_write,    "Write text line to file",               "write <file> <text>"},
    {"nano",     builtin_nano,     "Interactive terminal text editor",      "nano [file]"},
    {"sysinfo",  builtin_sysinfo,  "Display modern system information",     "sysinfo"},
    {"lscpu",    builtin_lscpu,    "Query CPU architecture from /proc/cpuinfo", "lscpu"},
    {"top",      builtin_top,      "Live kernel process & memory monitor",      "top [-n iterations]"},
    {"ps",       builtin_ps,       "List active processes in kernel table", "ps"},
    {"mem",      builtin_mem,      "Display physical RAM & heap metrics",   "mem"},
    {"spawn",    builtin_spawn,    "Spawn background worker task",          "spawn [name]"},
    {"kill",     builtin_kill,     "Terminate process by PID",              "kill <pid>"},
    {"echo",     builtin_echo,     "Display a line of text",                "echo [-n] [string...]"},
    {"clear",    builtin_clear,    "Clear the terminal screen",             "clear"},
    {"history",  builtin_history,  "Display command history",               "history"},
    {"type",     builtin_type,     "Display command classification",        "type <command>"},
    {"help",     builtin_help,     "Show help manual for built-ins",        "help"},
    {"observe",  builtin_observe,  "Toggle live kernel request tracing",    "observe on|off"},
    {"shutdown", builtin_shutdown, "Clean ACPI hardware power off",         "shutdown"},
    {"exit",     builtin_exit,     "Exit MyShell and shutdown system",      "exit [status]"},
=======
    {"cd",      builtin_cd,      "Change the current directory",          "cd [dir | ~ | -]"},
    {"pwd",     builtin_pwd,     "Print current working directory",       "pwd"},
    {"ls",      builtin_ls,      "List directory contents with colors",   "ls [-l] [-a] [path]"},
    {"cat",     builtin_cat,     "Concatenate and display files",         "cat [-n] <file...>"},
    {"mkdir",   builtin_mkdir,   "Create directory",                      "mkdir <dir...>"},
    {"touch",   builtin_touch,   "Create empty file or update timestamp", "touch <file...>"},
    {"nano",    builtin_nano,    "Interactive terminal text editor",      "nano [file]"},
    {"sysinfo", builtin_sysinfo, "Display modern system information",     "sysinfo"},
    {"lscpu",   builtin_lscpu,   "Query CPU architecture from /proc/cpuinfo", "lscpu"},
    {"top",     builtin_top,     "Live kernel process & memory monitor",      "top [-n iterations]"},
    {"ps",      builtin_ps,      "Report live process snapshot from /proc",   "ps"},
    {"echo",    builtin_echo,    "Display a line of text",                "echo [-n] [string...]"},
    {"clear",   builtin_clear,   "Clear the terminal screen",             "clear"},
    {"history", builtin_history, "Display command history",               "history"},
    {"type",    builtin_type,    "Display command classification",        "type <command>"},
    {"help",        builtin_help,        "Show help manual for built-ins",        "help"},
    {"sched-run",   builtin_sched_run,   "Execute jobs using custom CPU scheduler", "sched-run <fcfs|rr|priority> [args]"},
    {"sched-bench", builtin_sched_bench, "Benchmark & compare scheduling policies", "sched-bench [workload_size]"},
    {"inspect",     builtin_inspect,     "Expose Linux kernel subsystem decisions", "inspect <command>"},
    {"telemetry",   builtin_telemetry,   "Toggle live kernel resource telemetry badge", "telemetry [on|off]"},
    {"exit",        builtin_exit,        "Exit MyShell",                          "exit [status]"},
>>>>>>> 07cedf0be6f57713d507d53a6c5198da7886465b
};

static const int NUM_BUILTINS = sizeof(BUILTINS) / sizeof(BUILTINS[0]);

const BuiltinCommand *get_builtins_table(int *count)
{
    if (count)
        *count = NUM_BUILTINS;
    return BUILTINS;
}

int builtin_help(int argc, char **argv, ShellState *state)
{
    (void)argc;
    (void)argv;
    (void)state;

    printf("\n%sBuilt-in Command Suite:%s\n", COLOR_BOLD, COLOR_RESET);

    for (int i = 0; i < NUM_BUILTINS; i++) {
        printf("  %s%-10s%s %-32s %s(%s)%s\n",
               COLOR_BRIGHT_GREEN, BUILTINS[i].name, COLOR_RESET,
               BUILTINS[i].description,
               COLOR_DIM, BUILTINS[i].usage, COLOR_RESET);
    }

    printf("\n%sExternal Commands:%s\n", COLOR_BOLD, COLOR_RESET);
    printf("  Any system command (%sgit%s, %sgcc%s, %spython%s, %scurl%s, etc.) is executed\n",
           COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET, COLOR_CYAN, COLOR_RESET);
    printf("  as a separate process via POSIX fork() & execvp().\n\n");
    return 0;
}

bool is_builtin(const char *name)
{
    if (!name)
        return false;

    for (int i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(name, BUILTINS[i].name) == 0)
            return true;
    }
    return false;
}

int execute_builtin(const SimpleCommand *cmd, ShellState *state)
{
    if (!cmd || cmd->argc == 0 || !cmd->argv[0])
        return 0;

    for (int i = 0; i < NUM_BUILTINS; i++) {
        if (strcmp(cmd->argv[0], BUILTINS[i].name) == 0) {
            return BUILTINS[i].func(cmd->argc, cmd->argv, state);
        }
    }
    return -1;
}

