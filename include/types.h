#ifndef _KERNEL_TYPES_H
#define _KERNEL_TYPES_H

/* Standard integer types for freestanding x86-64 kernel */
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

typedef uint64_t           uintptr_t;
typedef int64_t            intptr_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;

#define NULL ((void *)0)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 202311L)
/* In C23, bool, true, and false are built-in keywords */
#else
#ifndef __bool_true_false_are_defined
#define __bool_true_false_are_defined 1
typedef _Bool bool;
#define true  1
#define false 0
#endif
#endif

#endif /* _KERNEL_TYPES_H */
