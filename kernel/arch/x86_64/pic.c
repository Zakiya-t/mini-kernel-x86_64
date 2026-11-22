#include "arch/x86_64/pic.h"
#include "arch/x86_64/io.h"
#include "core/kprintf.h"

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void pic_init(void) {
    /* Save current interrupt masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: Start initialization sequence in cascade mode */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /* ICW2: Remap IRQs: Master vector offset 32 (0x20), Slave vector offset 40 (0x28) */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* ICW3: Cascade setup - Master has slave on IRQ2 (0000 0100 = 4) */
    outb(PIC1_DATA, 0x04);
    io_wait();
    /* Slave identity cascade (0000 0010 = 2) */
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4: Operate in 8086/88 mode */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask all IRQs initially, except cascade IRQ2 */
    outb(PIC1_DATA, 0xFB); // 1111 1011 (IRQ2 cascade line enabled)
    outb(PIC2_DATA, 0xFF); // all slave IRQs masked

    (void)mask1;
    (void)mask2;

    KLOG_OK("8259 PIC remapped (Master: 0x20-0x27, Slave: 0x28-0x2F)");
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_disable(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
