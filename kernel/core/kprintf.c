#include "core/kprintf.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/vga.h"

static void kprint_char(char c) {
    serial_putchar(c);
    vga_putchar(c);
}

static void kprint_str(const char *s) {
    if (!s) s = "(null)";
    while (*s) {
        kprint_char(*s++);
    }
}

static void kprint_uint64_base(uint64_t val, int base, int width, char pad_char, bool uppercase) {
    char buf[65];
    int pos = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        buf[pos++] = '0';
    } else {
        while (val > 0) {
            buf[pos++] = digits[val % base];
            val /= base;
        }
    }

    int pad_len = width - pos;
    while (pad_len-- > 0) {
        kprint_char(pad_char);
    }

    while (pos > 0) {
        kprint_char(buf[--pos]);
    }
}

static void kprint_int64(int64_t val) {
    if (val < 0) {
        kprint_char('-');
        val = -val;
    }
    kprint_uint64_base((uint64_t)val, 10, 0, ' ', false);
}

void kvprintf(const char *fmt, va_list args) {
    if (!fmt) return;

    for (size_t i = 0; fmt[i] != '\0'; i++) {
        if (fmt[i] != '%') {
            kprint_char(fmt[i]);
            continue;
        }

        i++; // skip '%'
        if (fmt[i] == '\0') break;

        char pad_char = ' ';
        int width = 0;

        if (fmt[i] == '0') {
            pad_char = '0';
            i++;
        }

        while (fmt[i] >= '0' && fmt[i] <= '9') {
            width = width * 10 + (fmt[i] - '0');
            i++;
        }

        bool is_long = false;
        if (fmt[i] == 'l') {
            is_long = true;
            i++;
            if (fmt[i] == 'l') {
                i++;
            }
        }

        switch (fmt[i]) {
            case 'c': {
                char c = (char)va_arg(args, int);
                kprint_char(c);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                kprint_str(s);
                break;
            }
            case 'd':
            case 'i': {
                if (is_long) {
                    int64_t v = va_arg(args, int64_t);
                    kprint_int64(v);
                } else {
                    int32_t v = va_arg(args, int32_t);
                    kprint_int64((int64_t)v);
                }
                break;
            }
            case 'u': {
                if (is_long) {
                    uint64_t v = va_arg(args, uint64_t);
                    kprint_uint64_base(v, 10, width, pad_char, false);
                } else {
                    uint32_t v = va_arg(args, uint32_t);
                    kprint_uint64_base((uint64_t)v, 10, width, pad_char, false);
                }
                break;
            }
            case 'x': {
                if (is_long) {
                    uint64_t v = va_arg(args, uint64_t);
                    kprint_uint64_base(v, 16, width, pad_char, false);
                } else {
                    uint32_t v = va_arg(args, uint32_t);
                    kprint_uint64_base((uint64_t)v, 16, width, pad_char, false);
                }
                break;
            }
            case 'X': {
                if (is_long) {
                    uint64_t v = va_arg(args, uint64_t);
                    kprint_uint64_base(v, 16, width, pad_char, true);
                } else {
                    uint32_t v = va_arg(args, uint32_t);
                    kprint_uint64_base((uint64_t)v, 16, width, pad_char, true);
                }
                break;
            }
            case 'p': {
                uint64_t ptr = (uint64_t)va_arg(args, void *);
                kprint_str("0x");
                kprint_uint64_base(ptr, 16, 16, '0', false);
                break;
            }
            case '%': {
                kprint_char('%');
                break;
            }
            default: {
                kprint_char('%');
                kprint_char(fmt[i]);
                break;
            }
        }
    }
}

void kprintf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
}
