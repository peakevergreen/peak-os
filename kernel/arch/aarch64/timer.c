#include "arch.h"
#include "irq.h"
#include "platform.h"
#include "util.h"

static arch_timer_fn g_handler;
static uint32_t g_interval_ticks;

static void arch_timer_irq(void) {
    /* Acknowledge and re-arm */
    uint64_t cntfrq;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(cntfrq));
    if (!g_interval_ticks && cntfrq)
        g_interval_ticks = (uint32_t)(cntfrq / 100);
    __asm__ volatile ("msr cntp_tval_el0, %0" : : "r"((uint64_t)g_interval_ticks));
    __asm__ volatile ("msr cntp_ctl_el0, %0" : : "r"(1ull));
    if (g_handler)
        g_handler();
}

void aarch64_irq_handler(void) {
    /* Platform acknowledges (BCM local / GIC IAR) and dispatches registered
     * lines. Timer is installed as IRQ 0 via irq_install() below. */
    if (!platform_irq_ack_dispatch() && platform_timer_irq_pending())
        arch_timer_irq();
    platform_irq_eoi();
}

void arch_timer_init(uint32_t hz, arch_timer_fn handler) {
    g_handler = handler;
    if (hz == 0)
        hz = 100;
    uint64_t cntfrq = 0;
    __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(cntfrq));
    if (cntfrq == 0)
        cntfrq = 19200000; /* Pi typical */
    g_interval_ticks = (uint32_t)(cntfrq / hz);
    __asm__ volatile ("msr cntp_tval_el0, %0" : : "r"((uint64_t)g_interval_ticks));
    __asm__ volatile ("msr cntp_ctl_el0, %0" : : "r"(1ull));

    /* Enable timer IRQ at core (CNTPNSIRQ = PPI 30 / IRQ 27 depending on GIC).
     * BCM2837 uses local timer IRQ routing — platform enables the line. */
    platform_timer_enable();
    irq_install(0, arch_timer_irq);
}
