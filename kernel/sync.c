#include "sync.h"
#include "arch.h"
#include "util.h"

static int irq_depth;
static uint64_t irq_flags_saved;

uint64_t irq_save(void) {
    uint64_t flags = arch_irq_save();
    if (irq_depth == 0)
        irq_flags_saved = flags;
    irq_depth++;
    return flags;
}

void irq_restore(uint64_t flags) {
    (void)flags;
    if (irq_depth <= 0)
        return;
    irq_depth--;
    if (irq_depth == 0)
        arch_irq_restore(irq_flags_saved);
}

int irq_context(void) {
    return irq_depth > 0;
}

void spin_init(struct spinlock *lk, const char *name) {
    lk->locked = 0;
    lk->name = name ? name : "lock";
}

void spin_lock(struct spinlock *lk) {
    irq_save();
    /* Uniprocessor + irq disable: no true contention. */
    if (lk->locked) {
        /* Re-entrant take: keep held. */
    }
    lk->locked++;
    __asm__ volatile ("" ::: "memory");
}

void spin_unlock(struct spinlock *lk) {
    __asm__ volatile ("" ::: "memory");
    if (lk->locked)
        lk->locked--;
    irq_restore(0);
}
