#ifndef PEAK_NOTIFY_H
#define PEAK_NOTIFY_H

#include "types.h"

void notify_init(void);
void notify_clear(void);
void notify_push(const char *msg);
void notify_push_clipboard(const char *what);
int  notify_draw(uint32_t screen_w, uint32_t screen_h);
void notify_tick(void);
int  notify_active(void);
int  notify_consume_dirty(void);
void notify_bounds(uint32_t screen_w, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h);
void notify_dismiss(int display_idx);
void notify_dismiss_all(void);
int notify_click(int32_t mx, int32_t my, uint32_t screen_w);
int notify_history_count(void);
int notify_history_get(int idx, char *out, size_t cap);
void notify_history_clear(void);

#endif
