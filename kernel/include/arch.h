#ifndef PEAK_ARCH_H
#define PEAK_ARCH_H

#include "types.h"

struct peak_bootinfo;

/* CPU / interrupt / idle primitives (implemented per ARCH). */
void arch_early_init(void);
void arch_fpu_init(void);
void arch_cpu_init(void);          /* GDT/TSS or EL1 state */
void arch_irq_init(void);          /* IDT+PIC or exception vectors + IC */
void arch_irq_enable(void);
void arch_irq_disable(void);
void arch_idle(void);
uint64_t arch_irq_save(void);
void arch_irq_restore(uint64_t flags);

/* Timer: program hardware for ~hz ticks; call handler each tick. */
typedef void (*arch_timer_fn)(void);
void arch_timer_init(uint32_t hz, arch_timer_fn handler);

/* Serial backend used by serial_* */
void arch_serial_init(void);
void arch_serial_write(char c);

/* Optional: park secondary CPUs (no-op on uniprocessor x86). */
void arch_park_secondaries(struct peak_bootinfo *info);

/* VA→PA for kernel-image, HHDM, and identity addresses (wraps vmm;
 * valid on any ARCH once vmm_init has run). */
uint64_t arch_virt_to_phys(void *virt);

#endif
