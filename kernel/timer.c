#include "timer.h"
#include "idt.h"
#include "pic.h"
#include "util.h"

static volatile uint64_t ticks;

static void timer_irq(struct interrupt_frame *frame) {
    (void)frame;
    ticks++;
}

void timer_init(uint32_t hz) {
    ticks = 0;
    irq_install(0, timer_irq);

    uint32_t divisor = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    pic_unmask(0);
}

uint64_t timer_ticks(void) {
    return ticks;
}

uint64_t timer_uptime_secs(void) {
    return ticks / 100;
}
