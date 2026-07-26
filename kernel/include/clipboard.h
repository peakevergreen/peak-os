#ifndef PEAK_CLIPBOARD_H
#define PEAK_CLIPBOARD_H

#include "types.h"

#define CLIPBOARD_MAX 2048

void clipboard_init(void);
void clipboard_clear(void);
void clipboard_set(const char *text, size_t len);
size_t clipboard_get(char *buf, size_t cap);
size_t clipboard_get_previous(char *buf, size_t cap);
int clipboard_has(void);
void clipboard_set_ttl_ticks(uint64_t ticks);
int clipboard_history_count(void);
size_t clipboard_get_slot(int slot_idx, char *buf, size_t cap);
void clipboard_select_slot(int slot_idx);

#endif
