#ifndef PEAK_PMM_H
#define PEAK_PMM_H

#include "types.h"
#include <limine.h>

void pmm_init(struct limine_memmap_response *mmap, uint64_t hhdm_offset);
void *pmm_alloc(void);
void  pmm_free(void *phys);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_hhdm(void);

#endif
