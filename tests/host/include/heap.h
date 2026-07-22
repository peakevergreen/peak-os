#ifndef PEAK_HEAP_H
#define PEAK_HEAP_H

#include <types.h>

void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
uint64_t heap_total_allocated(void);

#endif
