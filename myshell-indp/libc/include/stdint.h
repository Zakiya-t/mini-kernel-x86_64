#ifndef _STDINT_H
#define _STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed short       int16_t;
typedef unsigned short     uint16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;

typedef int64_t            intptr_t;
typedef uint64_t           uintptr_t;

typedef uint64_t           size_t;
typedef int64_t            ssize_t;
typedef int64_t            off_t;
typedef int32_t            pid_t;
typedef uint32_t           mode_t;

#define INT32_MAX          (2147483647)
#define INT32_MIN          (-2147483647 - 1)
#define UINT32_MAX         (4294967295U)
#define INT64_MAX          (9223372036854775807LL)
#define INT64_MIN          (-9223372036854775807LL - 1LL)
#define UINT64_MAX         (18446744073709551615ULL)

#endif

