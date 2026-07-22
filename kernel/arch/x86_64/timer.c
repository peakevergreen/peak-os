#include "arch.h"
#include "irq.h"
#include "pic.h"
#include "util.h"

static arch_timer_fn g_handler;

static void pit_irq(void) {
    if (g_handler)
        g_handler();
}

void arch_timer_init(uint32_t hz, arch_timer_fn handler) {
    g_handler = handler;
    irq_install(0, pit_irq);

    if (hz == 0)
        hz = 100;
    uint32_t divisor = 1193182 / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    pic_unmask(0);
    irq_enable_line(0);
}
