#ifndef PEAK_SYNC_H
#define PEAK_SYNC_H

#include "types.h"

/* IRQ-safe spinlock: disables interrupts while held (uniprocessor-safe). */
struct spinlock {
    volatile uint32_t locked;
    const char *name;
};

void spin_init(struct spinlock *lk, const char *name);
void spin_lock(struct spinlock *lk);
void spin_unlock(struct spinlock *lk);

/* Nested cli/sti that restores prior IF flag */
uint64_t irq_save(void);
void     irq_restore(uint64_t flags);

/* Non-zero when servicing an interrupt (cli nest depth > 0). */
int irq_context(void);

#endif
