#ifndef _ARCH_X86_64_SERIAL_H
#define _ARCH_X86_64_SERIAL_H

#include "types.h"

#define COM1_PORT 0x3F8

int  serial_init(void);
void serial_putchar(char c);
void serial_write(const char *str);
void serial_write_len(const char *str, size_t len);
bool serial_has_data(void);
char serial_getchar(void);

#endif /* _ARCH_X86_64_SERIAL_H */
