#include "arch/x86_64/pit.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/pic.h"
#include "arch/x86_64/cpu.h"
#include "core/kprintf.h"

#define PIT_CHANNEL0_PORT 0x40
#define PIT_COMMAND_PORT  0x43

static volatile uint64_t timer_ticks = 0;
static uint32_t current_frequency = 0;

static void pit_irq_handler(interrupt_frame_t *frame) {
    timer_ticks++;
    extern void scheduler_tick(interrupt_frame_t *frame);
    scheduler_tick(frame);
}

void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) frequency_hz = 100;
    current_frequency = frequency_hz;

    uint32_t divisor = PIT_BASE_FREQUENCY / frequency_hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;

    /* Command Byte: Channel 0, Access mode lobyte/hibyte, Mode 3 (Square wave), Binary */
    outb(PIT_COMMAND_PORT, 0x36);

    /* Write divisor low byte then high byte */
    outb(PIT_CHANNEL0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    /* Register timer handler on IRQ0 (Vector 32) */
    irq_register_handler(0, pit_irq_handler);

    /* Unmask IRQ0 on 8259 PIC */
    pic_unmask_irq(0);

    KLOG_OK("8254 PIT initialized at %u Hz (Period: %u ms)", frequency_hz, 1000 / frequency_hz);
}

uint64_t pit_get_ticks(void) {
    return timer_ticks;
}

uint32_t pit_get_frequency(void) {
    return current_frequency;
}

void pit_sleep_ticks(uint64_t ticks) {
    uint64_t target = timer_ticks + ticks;
    while (timer_ticks < target) {
        hlt(); // Halt CPU until next interrupt tick saves power and avoids busy spinning
    }
}
