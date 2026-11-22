#include "syscall.h"
#include "errno.h"

int errno = 0;

int64_t sys_call(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    int64_t ret;
    register uint64_t r10 __asm__("r10") = a4;

    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );

    if (ret < 0 && ret >= -4095) {
        errno = (int)(-ret);
    }
    return ret;
}

