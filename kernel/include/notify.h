#ifndef PEAK_NOTIFY_H
#define PEAK_NOTIFY_H

#include "types.h"

void notify_init(void);
void notify_push(const char *msg);
/* Draw toast near top-right; returns 1 if something visible. */
int  notify_draw(uint32_t screen_w, uint32_t screen_h);
void notify_tick(void); /* age toasts */
int  notify_active(void);
/* Non-zero when a toast was created or expired since last check; clears the flag. */
int  notify_consume_dirty(void);
void notify_bounds(uint32_t screen_w, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);

#endif
