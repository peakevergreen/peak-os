#ifndef PEAK_HEAP_H
#define PEAK_HEAP_H

#include <types.h>

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
void heap_get_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *blocks_out);
uint32_t heap_fragmentation_pct(void);
#define HEAP_NCLASSES 9
struct heap_freelist_stats {
    uint32_t free_blocks;
    uint32_t freelist_heads;
    uint64_t largest_free;
    uint32_t class_counts[HEAP_NCLASSES];
};
void heap_get_freelist_stats(struct heap_freelist_stats *out);
uint32_t heap_oom_count(void);
uint64_t heap_total_allocated(void);

#endif
