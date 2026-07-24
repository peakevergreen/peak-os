#include "timer.h"
#include "arch.h"
#include "sched.h"
#include "random.h"

static volatile uint64_t ticks;
static volatile uint64_t irq_count;

static void timer_tick(void) {
    ticks++;
    irq_count++;
    /*
     * Entropy from the PIT is useful, but mixing on every 100 Hz tick is
     * wasted work on an idle cooperative UP box. Keyboard/mouse IRQs still
     * mix at full rate; sample the tick counter every 4th IRQ instead.
     */
    if ((ticks & 3u) == 0u)
        random_mix_irq(ticks ^ (ticks << 17));
    sched_on_timer();
}

void timer_init(uint32_t hz) {
    ticks = 0;
    irq_count = 0;
    arch_timer_init(hz, timer_tick);
}

uint64_t timer_ticks(void) {
    return ticks;
}

uint64_t timer_uptime_secs(void) {
    return ticks / 100;
}

uint64_t timer_irq_count(void) {
    return irq_count;
}
