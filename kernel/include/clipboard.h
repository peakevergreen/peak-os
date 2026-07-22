#ifndef PEAK_CLIPBOARD_H
#define PEAK_CLIPBOARD_H

#include "types.h"

#define CLIPBOARD_MAX 2048

void clipboard_init(void);
void clipboard_set(const char *text, size_t len);
size_t clipboard_get(char *buf, size_t cap);
int clipboard_has(void);
void clipboard_set_ttl_ticks(uint64_t ticks);

#endif
