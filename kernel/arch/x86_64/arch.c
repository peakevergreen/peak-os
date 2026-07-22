#include "arch.h"
#include "fpu.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "pic.h"
#include "peak_boot.h"
#include "util.h"
#include "vmm.h"

void arch_early_init(void) {
    fpu_init();
}

void arch_fpu_init(void) {
    fpu_init();
}

void arch_cpu_init(void) {
    vmm_enable_nx();
    gdt_init();
}

void arch_irq_init(void) {
    irq_init();
    pic_init();
    idt_init();
}

void arch_irq_enable(void) {
    sti();
}

void arch_irq_disable(void) {
    cli();
}

void arch_idle(void) {
    hlt();
}

uint64_t arch_irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

void arch_irq_restore(uint64_t flags) {
    if (flags & (1ull << 9))
        __asm__ volatile ("sti" ::: "memory");
}

void arch_park_secondaries(struct peak_bootinfo *info) {
    (void)info;
}
