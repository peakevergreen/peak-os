#include "arch.h"
#include "fpu.h"
#include "idt.h"
#include "irq.h"
#include "peak_boot.h"
#include "serial.h"
#include "syscall.h"
#include "util.h"
#include "vmm.h"

extern void exception_vectors(void);
extern uint64_t __boot_dtb;

/* Called from exception vectors with frame pointer in x0 / as arg. */
void aarch64_sync_entry(struct interrupt_frame *frame) {
    uint64_t esr = frame->esr;
    uint32_t ec = (uint32_t)((esr >> 26) & 0x3f);
    if (ec == 0x15) { /* SVC from AArch64 */
        syscall_handler(frame);
        return;
    }
    serial_write_str("aarch64 sync exception esr=");
    char buf[20];
    itoa_u(esr, buf, 16);
    serial_write_str(buf);
    serial_write_str(" elr=");
    itoa_u(frame->elr, buf, 16);
    serial_write_str(buf);
    serial_write_str("\n");
    for (;;)
        arch_idle();
}

void aarch64_irq_handler(void);

void arch_early_init(void) {
    /* Vectors installed in boot shim before kernel_entry. */
}

void arch_fpu_init(void) {
    fpu_init();
}

void arch_cpu_init(void) {
    /* EL1 already configured by boot shim; mark NX available for VMM flags. */
    vmm_enable_nx();
}

void arch_irq_init(void) {
    irq_init();
    __asm__ volatile ("msr vbar_el1, %0" : : "r"((uint64_t)exception_vectors));
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
    uint64_t daif;
    __asm__ volatile ("mrs %0, daif" : "=r"(daif));
    cli();
    return daif;
}

void arch_irq_restore(uint64_t flags) {
    /* bit 7 = I mask */
    if ((flags & (1ull << 7)) == 0)
        sti();
}

void arch_park_secondaries(struct peak_bootinfo *info) {
    (void)info;
    /* Secondaries already parked in start.S spin-table style WFE loop. */
}
