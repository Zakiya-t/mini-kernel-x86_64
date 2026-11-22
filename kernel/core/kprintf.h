#ifndef _CORE_KPRINTF_H
#define _CORE_KPRINTF_H

#include "types.h"
#include "ansi_colors.h"
#include <stdarg.h>

void kprintf(const char *fmt, ...);
void kvprintf(const char *fmt, va_list args);

/* Color-coded / categorized educational kernel log macros */
#define KLOG_INFO(fmt, ...)    kprintf(ANSI_BBLUE "[INFO]  " ANSI_RESET fmt "\n", ##__VA_ARGS__)
#define KLOG_OK(fmt, ...)      kprintf(ANSI_BGREEN "[OK]    " ANSI_RESET fmt "\n", ##__VA_ARGS__)
#define KLOG_WARN(fmt, ...)    kprintf(ANSI_BYELLOW "[WARN]  " ANSI_RESET fmt "\n", ##__VA_ARGS__)
#define KLOG_ERR(fmt, ...)     kprintf(ANSI_BRED "[ERR]   " ANSI_RESET fmt "\n", ##__VA_ARGS__)
#define KLOG_HW(fmt, ...)      kprintf(ANSI_BCYAN "[HW]    " ANSI_RESET fmt "\n", ##__VA_ARGS__)
#define KLOG_OBSERVE(fmt, ...) kprintf(ANSI_BMAGENTA "[TRACE] " ANSI_RESET fmt "\n", ##__VA_ARGS__)

#endif /* _CORE_KPRINTF_H */
