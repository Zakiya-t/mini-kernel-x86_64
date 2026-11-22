#include "sys/utsname.h"
#include "sys/ioctl.h"
#include "sys/select.h"
#include "termios.h"
#include "time.h"
#include "string.h"
#include "syscall.h"
#include "stdarg.h"

int uname(struct utsname *buf) {
    if (!buf) return -1;
    strncpy(buf->sysname, "MiniKernel", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "minikernel", sizeof(buf->nodename) - 1);
    strncpy(buf->release, "1.0.0", sizeof(buf->release) - 1);
    strncpy(buf->version, "x86-64 Ring 0 Kernel + Ring 3 Isolation", sizeof(buf->version) - 1);
    strncpy(buf->machine, "x86_64", sizeof(buf->machine) - 1);
    return 0;
}

int ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    if (request == TIOCGWINSZ) {
        va_list ap;
        va_start(ap, request);
        struct winsize *ws = va_arg(ap, struct winsize *);
        va_end(ap);
        if (ws) {
            ws->ws_col = 80;
            ws->ws_row = 25;
            ws->ws_xpixel = 640;
            ws->ws_ypixel = 400;
            return 0;
        }
    }
    return -1;
}

int tcgetattr(int fd, struct termios *termios_p) {
    (void)fd;
    if (termios_p) {
        memset(termios_p, 0, sizeof(struct termios));
    }
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)fd;
    (void)optional_actions;
    (void)termios_p;
    return 0;
}

time_t time(time_t *tloc) {
    time_t now = 0;
    if (tloc) *tloc = now;
    return now;
}

static struct tm static_tm = {
    .tm_sec = 0,
    .tm_min = 0,
    .tm_hour = 12,
    .tm_mday = 1,
    .tm_mon = 0,
    .tm_year = 126,
    .tm_wday = 4,
    .tm_yday = 0,
    .tm_isdst = 0
};

struct tm *localtime(const time_t *timep) {
    (void)timep;
    return &static_tm;
}

size_t strftime(char *s, size_t max, const char *format, const struct tm *tm) {
    (void)format;
    (void)tm;
    if (!s || max < 16) return 0;
    strncpy(s, "Nov 10 12:00", max - 1);
    s[max - 1] = '\0';
    return strlen(s);
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    (void)nfds;
    (void)readfds;
    (void)writefds;
    (void)exceptfds;
    (void)timeout;
    return 0;
}
