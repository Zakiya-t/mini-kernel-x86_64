#ifndef _ARCH_X86_64_IO_H
#define _ARCH_X86_64_IO_H

#include "types.h"

/**
 * io.h - x86-64 Port I/O Functions
 * 
 * Hardware devices on the x86 architecture (such as the 8259 PIC, 8254 PIT,
 * and 16550 UART serial controller) communicate with the CPU via isolated
 * I/O address space accessed using specialized assembly instructions:
 * IN (read from port) and OUT (write to port).
 */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * io_wait - Brief delay for older hardware devices to settle after I/O port writes.
 * Writes a dummy byte to port 0x80 (unused post code port).
 */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* _ARCH_X86_64_IO_H */
