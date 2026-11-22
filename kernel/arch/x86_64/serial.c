#include "arch/x86_64/serial.h"
#include "arch/x86_64/io.h"

#define COM1_DATA          (COM1_PORT + 0)
#define COM1_IER           (COM1_PORT + 1)
#define COM1_BAUD_LO       (COM1_PORT + 0)
#define COM1_BAUD_HI       (COM1_PORT + 1)
#define COM1_FCR           (COM1_PORT + 2)
#define COM1_LCR           (COM1_PORT + 3)
#define COM1_MCR           (COM1_PORT + 4)
#define COM1_LSR           (COM1_PORT + 5)

int serial_init(void) {
    /* Disable all UART interrupts during initialization */
    outb(COM1_IER, 0x00);

    /* Enable DLAB (Divisor Latch Access Bit) to set baud rate divisor */
    outb(COM1_LCR, 0x80);

    /* Set divisor to 3 -> 115200 / 3 = 38400 baud (standard serial speed) */
    outb(COM1_BAUD_LO, 0x03);
    outb(COM1_BAUD_HI, 0x00);

    /* 8 bits word length, no parity, one stop bit (8N1), DLAB disabled */
    outb(COM1_LCR, 0x03);

    /* Enable FIFO, clear transmit and receive FIFOs, 14-byte threshold */
    outb(COM1_FCR, 0xC7);

    /* Modem Control: Data Terminal Ready (DTR), Request To Send (RTS), Out2 enabled */
    outb(COM1_MCR, 0x0B);

    /* Hardware Loopback Test to verify UART functionality */
    outb(COM1_MCR, 0x1E);       /* Set in loopback mode */
    outb(COM1_DATA, 0xAE);      /* Send test byte */

    if (inb(COM1_DATA) != 0xAE) {
        return -1; /* Serial chip failed hardware loopback test */
    }

    /* Restore normal operation mode (DTR, RTS, Out2, no loopback) */
    outb(COM1_MCR, 0x0F);
    return 0;
}

static bool serial_is_transmit_empty(void) {
    return (inb(COM1_LSR) & 0x20) != 0;
}

void serial_putchar(char c) {
    /* Convert newline to CR+LF for standard serial terminals */
    if (c == '\n') {
        while (!serial_is_transmit_empty());
        outb(COM1_DATA, '\r');
    }
    while (!serial_is_transmit_empty());
    outb(COM1_DATA, (uint8_t)c);
}

void serial_write(const char *str) {
    if (!str) return;
    while (*str) {
        serial_putchar(*str++);
    }
}

void serial_write_len(const char *str, size_t len) {
    if (!str) return;
    for (size_t i = 0; i < len; i++) {
        serial_putchar(str[i]);
    }
}

bool serial_has_data(void) {
    return (inb(COM1_LSR) & 0x01) != 0;
}

char serial_getchar(void) {
    while (!serial_has_data()) {
        __asm__ volatile ("sti\n\thlt");
    }
    return (char)inb(COM1_DATA);
}
