#ifndef PEAK_IRQ_H
#define PEAK_IRQ_H

#include "types.h"

/* Portable IRQ registration. Numbers are platform-defined:
 * x86: 0..15 are PIC IRQs (timer=0, kbd=1, mouse=12).
 * aarch64/rpi: DT / SoC SPI or BCM IRQ numbers. */
#define PEAK_IRQ_MAX 256

typedef void (*irq_handler_t)(void);

void irq_init(void);
void irq_install(uint32_t irq, irq_handler_t handler);
void irq_uninstall(uint32_t irq);
void irq_enable_line(uint32_t irq);
void irq_disable_line(uint32_t irq);
void irq_dispatch(uint32_t irq);

/* Arch may call this from the vector / PIC path. */
irq_handler_t irq_get_handler(uint32_t irq);

#endif
