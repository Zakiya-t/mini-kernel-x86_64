#include "unistd.h"
#include "fcntl.h"
#include "syscall.h"
#include "string.h"
#include "errno.h"

int open(const char *pathname, int flags, ...) {
    if (!pathname) return -1;
    int64_t ret = sys_call(SYS_OPEN, (uint64_t)pathname, (uint64_t)flags, 0, 0);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return (int)ret;
}

ssize_t read(int fd, void *buf, size_t count) {
    int64_t ret = sys_call(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count, 0);
    return (ssize_t)ret;
}

ssize_t write(int fd, const void *buf, size_t count) {
    int64_t ret = sys_call(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count, 0);
    return (ssize_t)ret;
}

int close(int fd) {
    if (fd >= 0 && fd <= 2) return 0;
    return (int)sys_call(SYS_CLOSE, (uint64_t)fd, 0, 0, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
    int64_t ret = sys_call(SYS_SEEK, (uint64_t)fd, (uint64_t)offset, (uint64_t)whence, 0);
    return (off_t)ret;
}

int chdir(const char *path) {
    if (!path) return -1;
    return (int)sys_call(SYS_CHDIR, (uint64_t)path, 0, 0, 0);
}

char *getcwd(char *buf, size_t size) {
    if (!buf || size == 0) return NULL;
    int64_t ret = sys_call(SYS_GETCWD, (uint64_t)buf, (uint64_t)size, 0, 0);
    if (ret < 0) return NULL;
    return buf;
}

int unlink(const char *pathname) {
    if (!pathname) return -1;
    return (int)sys_call(SYS_FS_UNLINK, (uint64_t)pathname, 0, 0, 0);
}

pid_t getpid(void) {
    return (pid_t)sys_call(SYS_GETPID, 0, 0, 0, 0);
}

pid_t fork(void) {
    return (pid_t)sys_call(SYS_FORK, 0, 0, 0, 0);
}

int execvp(const char *file, char *const argv[]) {
    if (!file) return -1;

    char cmdline[256];
    cmdline[0] = '\0';
    strncpy(cmdline, file, sizeof(cmdline) - 1);

    if (argv) {
        for (int i = 1; argv[i] != NULL; i++) {
            strncat(cmdline, " ", sizeof(cmdline) - strlen(cmdline) - 1);
            strncat(cmdline, argv[i], sizeof(cmdline) - strlen(cmdline) - 1);
        }
    }

    int64_t ret = sys_call(SYS_EXEC, (uint64_t)cmdline, 0, 0, 0);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

int dup(int oldfd) {
    return oldfd;
}

int dup2(int oldfd, int newfd) {
    (void)oldfd;
    return newfd;
}

int pipe(int pipefd[2]) {
    if (!pipefd) return -1;
    pipefd[0] = 0;
    pipefd[1] = 1;
    return 0;
}

pid_t waitpid(pid_t pid, int *status, int options) {
    (void)options;
    if (status) *status = 0;
    if (pid <= 0) return -1;

    /* Poll / yield in a loop until the child process is reaped */
    while (1) {
        int64_t ret = sys_call(SYS_REAP, (uint64_t)pid, 0, 0, 0);
        if (ret == 0) {
            /* Child process successfully reaped */
            break;
        }
        if (ret == -3) {
            /* Child process not found (already exited or invalid) */
            break;
        }
        /* Child is still active (ret == -2): yield CPU to allow child to run */
        sys_call(SYS_YIELD, 0, 0, 0, 0);
    }
    return pid;
}

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}
