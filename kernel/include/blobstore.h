#ifndef PEAK_BLOBSTORE_H
#define PEAK_BLOBSTORE_H

#include "types.h"

/* Block-backed object store with a bounded RAM page cache.
 * Data lives on disk; the kernel keeps a small LRU cache so RAM stays slim
 * while total capacity scales with the block device. */

#define BLOBSTORE_PAGE_SIZE   4096u
#ifndef BLOBSTORE_CACHE_PAGES
#define BLOBSTORE_CACHE_PAGES 32u   /* 128 KiB RAM cache; override in host tests */
#endif
#define BLOBSTORE_MAX_OBJECTS 256u
#define BLOBSTORE_LBA_BASE    8192u /* 4 MiB — PeakFS image lives below */

void blobstore_init(void);
int  blobstore_available(void);

/* Create an object of at least `size` bytes. Returns 0 and sets *out_id. */
int  blobstore_create(uint32_t *out_id, size_t size);
int  blobstore_delete(uint32_t id);
int  blobstore_resize(uint32_t id, size_t new_size);
size_t blobstore_size(uint32_t id);

int  blobstore_read(uint32_t id, size_t off, void *buf, size_t len);
int  blobstore_write(uint32_t id, size_t off, const void *buf, size_t len);

/* Persist / reload the object table (called from peakdisk save/load). */
int  blobstore_sync(void);
int  blobstore_load(void);

uint32_t blobstore_object_count(void);
uint32_t blobstore_cache_pages_used(void);

#endif
