#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"
#include "errno.h"
#include "stdbool.h"
#include "ctype.h"

struct FILE {
    int    fd;
    int    flags;
    bool   is_virtual;
    char  *virtual_buf;
    size_t virtual_len;
    size_t virtual_pos;
    bool   eof;
    bool   error;
};

static FILE std_streams[3] = {
    { .fd = 0, .flags = USER_O_RDONLY, .is_virtual = false, .virtual_buf = NULL, .virtual_len = 0, .virtual_pos = 0, .eof = false, .error = false },
    { .fd = 1, .flags = USER_O_WRONLY, .is_virtual = false, .virtual_buf = NULL, .virtual_len = 0, .virtual_pos = 0, .eof = false, .error = false },
    { .fd = 2, .flags = USER_O_WRONLY, .is_virtual = false, .virtual_buf = NULL, .virtual_len = 0, .virtual_pos = 0, .eof = false, .error = false }
};

FILE *stdin  = &std_streams[0];
FILE *stdout = &std_streams[1];
FILE *stderr = &std_streams[2];

/* ── Character I/O ───────────────────────────────────────────────────────── */
int putchar(int c) {
    char ch = (char)c;
    sys_call(SYS_WRITE, 1, (uint64_t)&ch, 1, 0);
    return (unsigned char)ch;
}

int getchar(void) {
    char ch = 0;
    int64_t n = sys_call(SYS_READ, 0, (uint64_t)&ch, 1, 0);
    if (n <= 0) return EOF;
    return (unsigned char)ch;
}

int puts(const char *s) {
    if (!s) return EOF;
    size_t len = strlen(s);
    sys_call(SYS_WRITE, 1, (uint64_t)s, len, 0);
    putchar('\n');
    return (int)len + 1;
}

int fputs(const char *s, FILE *stream) {
    if (!s || !stream) return EOF;
    size_t len = strlen(s);
    if (stream->is_virtual) {
        return (int)len;
    }
    int64_t n = sys_call(SYS_WRITE, (uint64_t)stream->fd, (uint64_t)s, len, 0);
    return (n >= 0) ? (int)n : EOF;
}

char *fgets(char *s, int size, FILE *stream) {
    if (!s || size <= 0 || !stream) return NULL;

    /* Interactive reading from stdin (fd 0) with line editing and echo */
    if (stream->fd == 0 && !stream->is_virtual) {
        int pos = 0;
        while (pos < size - 1) {
            char c = 0;
            int64_t n = sys_call(SYS_READ, 0, (uint64_t)&c, 1, 0);
            if (n <= 0) {
                if (pos == 0) return NULL;
                break;
            }

            /* Enter */
            if (c == '\r' || c == '\n') {
                s[pos++] = '\n';
                putchar('\n');
                break;
            }

            /* Backspace / Delete */
            if (c == '\b' || c == 127) {
                if (pos > 0) {
                    pos--;
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                }
                continue;
            }

            /* Printable characters */
            if (c >= 32 && c <= 126) {
                s[pos++] = c;
                putchar(c);
            }
        }
        s[pos] = '\0';
        return s;
    }

    /* Reading from virtual file (e.g. /proc/meminfo or /proc/cpuinfo) */
    if (stream->is_virtual) {
        if (stream->virtual_pos >= stream->virtual_len) {
            stream->eof = true;
            return NULL;
        }
        int pos = 0;
        while (pos < size - 1 && stream->virtual_pos < stream->virtual_len) {
            char c = stream->virtual_buf[stream->virtual_pos++];
            s[pos++] = c;
            if (c == '\n') break;
        }
        s[pos] = '\0';
        return s;
    }

    /* Reading from disk file (fd >= 3) */
    int pos = 0;
    while (pos < size - 1) {
        char c = 0;
        int64_t n = sys_call(SYS_READ, (uint64_t)stream->fd, (uint64_t)&c, 1, 0);
        if (n <= 0) {
            if (pos == 0) {
                stream->eof = true;
                return NULL;
            }
            break;
        }
        s[pos++] = c;
        if (c == '\n') break;
    }
    s[pos] = '\0';
    return s;
}

/* ── File Management ─────────────────────────────────────────────────────── */
FILE *fopen(const char *pathname, const char *mode) {
    if (!pathname || !mode) return NULL;

    /* Synthesize /proc/meminfo */
    if (strcmp(pathname, "/proc/meminfo") == 0) {
        user_mem_stats_t st;
        memset(&st, 0, sizeof(st));
        sys_call(SYS_MEM_STATS, (uint64_t)(uintptr_t)&st, 0, 0, 0);

        FILE *f = (FILE *)malloc(sizeof(FILE));
        if (!f) return NULL;
        f->fd = -1;
        f->is_virtual = true;
        f->virtual_pos = 0;
        f->eof = false;
        f->error = false;

        f->virtual_buf = (char *)malloc(512);
        if (!f->virtual_buf) { free(f); return NULL; }

        long total_kb = (long)(st.total_phys_ram / 1024);
        long free_kb  = (long)(st.free_phys_ram / 1024);
        if (total_kb <= 0) total_kb = 524288;
        if (free_kb <= 0)  free_kb  = 491520;

        snprintf(f->virtual_buf, 512,
                 "MemTotal:       %ld kB\n"
                 "MemFree:        %ld kB\n"
                 "MemAvailable:   %ld kB\n"
                 "Buffers:           16384 kB\n"
                 "Cached:            32768 kB\n",
                 total_kb, free_kb, free_kb);
        f->virtual_len = strlen(f->virtual_buf);
        return f;
    }

    /* Synthesize /proc/cpuinfo */
    if (strcmp(pathname, "/proc/cpuinfo") == 0) {
        FILE *f = (FILE *)malloc(sizeof(FILE));
        if (!f) return NULL;
        f->fd = -1;
        f->is_virtual = true;
        f->virtual_pos = 0;
        f->eof = false;
        f->error = false;

        f->virtual_buf = (char *)malloc(1024);
        if (!f->virtual_buf) { free(f); return NULL; }

        snprintf(f->virtual_buf, 1024,
                 "processor\t: 0\n"
                 "vendor_id\t: GenuineIntel\n"
                 "cpu family\t: 6\n"
                 "model\t\t: 1\n"
                 "model name\t: x86-64 MiniKernel Native CPU (QEMU)\n"
                 "stepping\t: 3\n"
                 "cpu MHz\t\t: 2400.000\n"
                 "cache size\t: 4096 KB\n"
                 "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ss syscall nx pdpe1gb rdtscp lm\n"
                 "bogomips\t: 4800.00\n");
        f->virtual_len = strlen(f->virtual_buf);
        return f;
    }

    /* Standard disk file */
    int flags = 0;
    if (strchr(mode, '+')) {
        flags = USER_O_RDWR | USER_O_CREAT;
    } else if (strchr(mode, 'w')) {
        flags = USER_O_WRONLY | USER_O_CREAT | USER_O_TRUNC;
    } else if (strchr(mode, 'a')) {
        flags = USER_O_WRONLY | USER_O_CREAT | USER_O_APPEND;
    } else {
        flags = USER_O_RDONLY;
    }

    int64_t fd = sys_call(SYS_OPEN, (uint64_t)pathname, (uint64_t)flags, 0, 0);
    if (fd < 0) {
        return NULL;
    }

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        sys_call(SYS_CLOSE, (uint64_t)fd, 0, 0, 0);
        return NULL;
    }

    f->fd = (int)fd;
    f->flags = flags;
    f->is_virtual = false;
    f->virtual_buf = NULL;
    f->virtual_len = 0;
    f->virtual_pos = 0;
    f->eof = false;
    f->error = false;
    return f;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    if (stream == stdin || stream == stdout || stream == stderr) {
        return 0;
    }
    if (stream->is_virtual) {
        if (stream->virtual_buf) free(stream->virtual_buf);
        free(stream);
        return 0;
    }
    int64_t ret = sys_call(SYS_CLOSE, (uint64_t)stream->fd, 0, 0, 0);
    free(stream);
    return (ret >= 0) ? 0 : EOF;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || size == 0 || nmemb == 0 || !stream) return 0;
    size_t total = size * nmemb;

    if (stream->is_virtual) {
        if (stream->virtual_pos >= stream->virtual_len) return 0;
        size_t avail = stream->virtual_len - stream->virtual_pos;
        size_t to_read = (total < avail) ? total : avail;
        memcpy(ptr, stream->virtual_buf + stream->virtual_pos, to_read);
        stream->virtual_pos += to_read;
        return to_read / size;
    }

    int64_t n = sys_call(SYS_READ, (uint64_t)stream->fd, (uint64_t)ptr, total, 0);
    if (n <= 0) {
        stream->eof = true;
        return 0;
    }
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    if (!ptr || size == 0 || nmemb == 0 || !stream) return 0;
    size_t total = size * nmemb;
    if (stream->is_virtual) return 0;

    int64_t n = sys_call(SYS_WRITE, (uint64_t)stream->fd, (uint64_t)ptr, total, 0);
    if (n <= 0) {
        stream->error = true;
        return 0;
    }
    return (size_t)n / size;
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream) return -1;
    if (stream->is_virtual) {
        if (whence == 0) stream->virtual_pos = (size_t)offset;
        else if (whence == 1) stream->virtual_pos += (size_t)offset;
        else if (whence == 2) stream->virtual_pos = stream->virtual_len + offset;
        if (stream->virtual_pos > stream->virtual_len) stream->virtual_pos = stream->virtual_len;
        return 0;
    }
    int64_t ret = sys_call(SYS_SEEK, (uint64_t)stream->fd, (uint64_t)offset, (uint64_t)whence, 0);
    return (ret >= 0) ? 0 : -1;
}

long ftell(FILE *stream) {
    if (!stream) return -1;
    if (stream->is_virtual) return (long)stream->virtual_pos;
    return (long)sys_call(SYS_SEEK, (uint64_t)stream->fd, 0, 1 /* SEEK_CUR */, 0);
}

void rewind(FILE *stream) {
    fseek(stream, 0, 0);
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

void perror(const char *s) {
    if (s && *s) {
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    } else {
        fprintf(stderr, "%s\n", strerror(errno));
    }
}

int remove(const char *pathname) {
    return (int)sys_call(SYS_FS_UNLINK, (uint64_t)pathname, 0, 0, 0);
}

/* ── Formatting Engine (vsnprintf / printf) ──────────────────────────────── */
static void buf_putc(char *buf, size_t size, size_t *written, char c) {
    if (*written + 1 < size) {
        buf[*written] = c;
    }
    (*written)++;
}

static void buf_puts(char *buf, size_t size, size_t *written, const char *s, int width, bool left_align, int precision) {
    if (!s) s = "(null)";
    size_t len = strlen(s);
    if (precision >= 0 && (size_t)precision < len) {
        len = (size_t)precision;
    }
    int pad = (width > (int)len) ? (width - (int)len) : 0;

    if (!left_align) {
        while (pad-- > 0) buf_putc(buf, size, written, ' ');
    }
    for (size_t i = 0; i < len; i++) {
        buf_putc(buf, size, written, s[i]);
    }
    if (left_align) {
        while (pad-- > 0) buf_putc(buf, size, written, ' ');
    }
}

static void buf_put_int(char *buf, size_t size, size_t *written, int64_t val, int base, bool uppercase, int width, bool pad_zero, bool left_align) {
    char digits[32];
    int dpos = 0;
    bool neg = false;
    uint64_t uval;

    if (base == 10 && val < 0) {
        neg = true;
        uval = (uint64_t)(-val);
    } else {
        uval = (uint64_t)val;
    }

    const char *charset = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    if (uval == 0) {
        digits[dpos++] = '0';
    } else {
        while (uval > 0) {
            digits[dpos++] = charset[uval % base];
            uval /= base;
        }
    }

    int total_len = dpos + (neg ? 1 : 0);
    int pad = (width > total_len) ? (width - total_len) : 0;

    if (!left_align && !pad_zero) {
        while (pad-- > 0) buf_putc(buf, size, written, ' ');
    }
    if (neg) {
        buf_putc(buf, size, written, '-');
    }
    if (!left_align && pad_zero) {
        while (pad-- > 0) buf_putc(buf, size, written, '0');
    }
    while (dpos > 0) {
        buf_putc(buf, size, written, digits[--dpos]);
    }
    if (left_align) {
        while (pad-- > 0) buf_putc(buf, size, written, ' ');
    }
}

static void buf_put_float(char *buf, size_t size, size_t *written, double val, int prec, int width) {
    if (val < 0) {
        buf_putc(buf, size, written, '-');
        val = -val;
    }
    int64_t int_part = (int64_t)val;
    buf_put_int(buf, size, written, int_part, 10, false, 0, false, false);
    if (prec > 0) {
        buf_putc(buf, size, written, '.');
        double frac = val - (double)int_part;
        for (int i = 0; i < prec; i++) {
            frac *= 10.0;
            int digit = (int)frac;
            buf_putc(buf, size, written, '0' + (digit % 10));
            frac -= (double)digit;
        }
    }
    (void)width;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (!format) return 0;
    size_t written = 0;

    for (const char *p = format; *p; p++) {
        if (*p != '%') {
            buf_putc(str, size, &written, *p);
            continue;
        }

        p++;
        bool left_align = false;
        bool pad_zero = false;
        int width = 0;
        int precision = -1;
        bool is_long = false;
        bool is_long_long = false;

        if (*p == '-') { left_align = true; p++; }
        if (*p == '0') { pad_zero = true; p++; }

        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        if (*p == '.') {
            p++;
            if (*p == '*') {
                precision = va_arg(ap, int);
                p++;
            } else {
                precision = 0;
                while (*p >= '0' && *p <= '9') {
                    precision = precision * 10 + (*p - '0');
                    p++;
                }
            }
        }

        if (*p == 'l') {
            is_long = true;
            p++;
            if (*p == 'l') { is_long_long = true; p++; }
        }

        switch (*p) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                buf_puts(str, size, &written, s, width, left_align, precision);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                buf_putc(str, size, &written, c);
                break;
            }
            case 'd':
            case 'i': {
                int64_t val = (is_long || is_long_long) ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
                buf_put_int(str, size, &written, val, 10, false, width, pad_zero, left_align);
                break;
            }
            case 'u': {
                uint64_t val = (is_long || is_long_long) ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                buf_put_int(str, size, &written, (int64_t)val, 10, false, width, pad_zero, left_align);
                break;
            }
            case 'x': {
                uint64_t val = (is_long || is_long_long) ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                buf_put_int(str, size, &written, (int64_t)val, 16, false, width, pad_zero, left_align);
                break;
            }
            case 'X': {
                uint64_t val = (is_long || is_long_long) ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
                buf_put_int(str, size, &written, (int64_t)val, 16, true, width, pad_zero, left_align);
                break;
            }
            case 'p': {
                void *ptr = va_arg(ap, void *);
                buf_puts(str, size, &written, "0x", 0, false, -1);
                buf_put_int(str, size, &written, (int64_t)(uintptr_t)ptr, 16, false, 0, false, false);
                break;
            }
            case 'f': {
                double val = va_arg(ap, double);
                int prec = (precision >= 0) ? precision : 6;
                buf_put_float(str, size, &written, val, prec, width);
                break;
            }
            case '%': {
                buf_putc(str, size, &written, '%');
                break;
            }
            default: {
                buf_putc(str, size, &written, '%');
                buf_putc(str, size, &written, *p);
                break;
            }
        }
    }

    if (size > 0) {
        if (written < size) {
            str[written] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }
    return (int)written;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, (size_t)-1, format, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, (size_t)-1, format, ap);
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    char buf[2048];
    int n = vsnprintf(buf, sizeof(buf), format, ap);
    if (n > 0 && stream) {
        int fd = (stream == stderr) ? 2 : ((stream == stdout) ? 1 : stream->fd);
        sys_call(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (size_t)n, 0);
    }
    return n;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *format, va_list ap) {
    return vfprintf(stdout, format, ap);
}

int printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vfprintf(stdout, format, ap);
    va_end(ap);
    return ret;
}

int vsscanf(const char *str, const char *format, va_list ap) {
    if (!str || !format) return 0;
    int matched = 0;
    const char *s = str;
    const char *f = format;

    while (*f && *s) {
        if (isspace((unsigned char)*f)) {
            while (isspace((unsigned char)*f)) f++;
            while (isspace((unsigned char)*s)) s++;
            continue;
        }

        if (*f != '%') {
            if (*f == *s) { f++; s++; continue; }
            break;
        }

        f++;
        bool is_long = false;
        bool is_long_long = false;
        if (*f == 'l') {
            is_long = true;
            f++;
            if (*f == 'l') { is_long_long = true; f++; }
        }

        while (isspace((unsigned char)*s)) s++;

        if (*f == 'd' || *f == 'i') {
            char *end;
            long long val = strtoll(s, &end, 10);
            if (end == s) break;
            s = end;
            if (is_long_long) {
                *(va_arg(ap, long long *)) = val;
            } else if (is_long) {
                *(va_arg(ap, long *)) = (long)val;
            } else {
                *(va_arg(ap, int *)) = (int)val;
            }
            matched++;
            f++;
        } else if (*f == 'u') {
            char *end;
            unsigned long long val = (unsigned long long)strtoull(s, &end, 10);
            if (end == s) break;
            s = end;
            if (is_long_long) {
                *(va_arg(ap, unsigned long long *)) = val;
            } else if (is_long) {
                *(va_arg(ap, unsigned long *)) = (unsigned long)val;
            } else {
                *(va_arg(ap, unsigned int *)) = (unsigned int)val;
            }
            matched++;
            f++;
        } else if (*f == 's') {
            char *out = va_arg(ap, char *);
            while (*s && !isspace((unsigned char)*s)) {
                *out++ = *s++;
            }
            *out = '\0';
            matched++;
            f++;
        } else if (*f == 'c') {
            char *out = va_arg(ap, char *);
            *out = *s++;
            matched++;
            f++;
        } else {
            f++;
        }
    }
    return matched;
}

int sscanf(const char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsscanf(str, format, ap);
    va_end(ap);
    return ret;
}
