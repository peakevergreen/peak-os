#include "irq.h"
#include "util.h"

static irq_handler_t handlers[PEAK_IRQ_MAX];

void irq_init(void) {
    memset(handlers, 0, sizeof(handlers));
}

void irq_install(uint32_t irq, irq_handler_t handler) {
    if (irq < PEAK_IRQ_MAX)
        handlers[irq] = handler;
}

void irq_uninstall(uint32_t irq) {
    if (irq < PEAK_IRQ_MAX)
        handlers[irq] = 0;
}

irq_handler_t irq_get_handler(uint32_t irq) {
    if (irq >= PEAK_IRQ_MAX)
        return 0;
    return handlers[irq];
}

void irq_dispatch(uint32_t irq) {
    if (irq < PEAK_IRQ_MAX && handlers[irq])
        handlers[irq]();
}

/* Default line enable/disable — arch/platform may override via weak or
 * replace these in arch sources. x86 maps to pic_unmask/mask. */
__attribute__((weak)) void irq_enable_line(uint32_t irq) {
    (void)irq;
}

__attribute__((weak)) void irq_disable_line(uint32_t irq) {
    (void)irq;
}
