#ifndef _STDDEF_H
#define _STDDEF_H

#include "stdint.h"

typedef int64_t ptrdiff_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif

