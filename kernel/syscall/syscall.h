#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include "types.h"
#include "arch/x86_64/idt.h"
#include "syscall_nums.h"

void syscall_init(void);
void syscall_dispatcher(interrupt_frame_t *frame);

/* Live lifecycle observer toggle */
void syscall_set_observer(bool enabled);
bool syscall_is_observer_enabled(void);

#endif /* _KERNEL_SYSCALL_H */
