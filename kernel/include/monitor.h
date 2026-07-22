#ifndef PEAK_MONITOR_H
#define PEAK_MONITOR_H

#include "types.h"

void monitor_reset(void);
void monitor_tick(void);
void monitor_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int  monitor_wants_redraw(void);
void monitor_clear_redraw(void);
void monitor_input(char c); /* r = reset history */

#endif
