#ifndef PEAK_TIMER_H
#define PEAK_TIMER_H

#include "types.h"

void timer_init(uint32_t hz);
uint64_t timer_ticks(void);
uint64_t timer_uptime_secs(void);

#endif
