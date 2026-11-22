#ifndef _UNISTD_H
#define _UNISTD_H

#include "stdint.h"
#include "stddef.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int     close(int fd);
off_t   lseek(int fd, off_t offset, int whence);

int     chdir(const char *path);
char   *getcwd(char *buf, size_t size);
int     unlink(const char *pathname);

pid_t   getpid(void);
pid_t   fork(void);
int     execvp(const char *file, char *const argv[]);

int     isatty(int fd);
int     dup(int oldfd);
int     dup2(int oldfd, int newfd);
int     pipe(int pipefd[2]);

#endif

