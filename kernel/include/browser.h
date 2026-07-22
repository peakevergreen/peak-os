#ifndef PEAK_BROWSER_H
#define PEAK_BROWSER_H

#include "types.h"

void browser_reset(void);
void browser_input(char c);
void browser_draw(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void browser_go(const char *url);
/* Click in browser content coords (relative to draw origin). */
void browser_click(int32_t lx, int32_t ly, uint32_t w, uint32_t h);
int  browser_wants_redraw(void);
/* Drain per-tab JS timers/microtasks; may set wants_redraw. */
void browser_tick(void);
/* Aggregate JS metrics across tabs (for Monitor). */
void browser_js_metrics(uint32_t *tabs_with_js, uint32_t *objs, uint32_t *timers,
                        uint32_t *gc_runs);

#endif
