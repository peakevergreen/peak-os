#ifndef PEAK_HEAP_H
#define PEAK_HEAP_H

#include <types.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
void heap_get_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *blocks_out);
uint64_t heap_total_allocated(void);

#endif
