#ifndef PEAK_PMM_H
#define PEAK_PMM_H

#include "types.h"
#include "peak_boot.h"

void pmm_init(struct peak_bootinfo *info);
void *pmm_alloc(void);
void *pmm_alloc_pages(size_t n); /* contiguous pages; NULL on failure */
void  pmm_free(void *phys);
void  pmm_free_n(void *phys, size_t n); /* free n contiguous pages */
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_hhdm(void);

#endif
