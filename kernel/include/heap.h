#ifndef PEAK_HEAP_H
#define PEAK_HEAP_H

#include "types.h"

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t size);
void  kfree(void *ptr);
/* Zero payload then free (best-effort secret scrub). */
void  kfree_sensitive(void *ptr, size_t len);
void heap_get_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *blocks_out);
/* free_blocks / total free bytes → approximate fragmentation (0–100). */
uint32_t heap_fragmentation_pct(void);
/* Peak JS / browser accounting helpers. */
uint64_t heap_total_allocated(void);

#endif
