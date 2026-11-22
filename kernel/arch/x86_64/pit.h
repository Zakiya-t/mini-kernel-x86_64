#ifndef _ARCH_X86_64_PIT_H
#define _ARCH_X86_64_PIT_H

#include "types.h"
#include "arch/x86_64/idt.h"

#define PIT_BASE_FREQUENCY 1193182

void     pit_init(uint32_t frequency_hz);
uint64_t pit_get_ticks(void);
uint32_t pit_get_frequency(void);
void     pit_sleep_ticks(uint64_t ticks);

#endif /* _ARCH_X86_64_PIT_H */
