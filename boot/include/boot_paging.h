#ifndef PEAK_BOOT_PAGING_H
#define PEAK_BOOT_PAGING_H

#include <stdint.h>
#include "boot_util.h"
#include "peak_boot.h"

#define BOOT_PAGE_SIZE 4096ULL

struct boot_page_allocator {
    uint64_t (*alloc_pages)(size_t n); /* returns phys */
};

/* Identity-map [0, identity_end) with 2MiB pages where possible, plus HHDM. */
int boot_paging_init(struct boot_page_allocator *alloc,
                     uint64_t identity_end,
                     uint64_t *pml4_phys_out);

int boot_map_page(uint64_t pml4_phys, uint64_t virt, uint64_t phys, int writable,
                  struct boot_page_allocator *alloc);

int boot_map_range(uint64_t pml4_phys, uint64_t virt, uint64_t phys, uint64_t size,
                   int writable, struct boot_page_allocator *alloc);

void boot_paging_activate(uint64_t pml4_phys);

#endif
