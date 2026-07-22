#ifndef PEAK_BOOT_ELF_H
#define PEAK_BOOT_ELF_H

#include <stdint.h>
#include "boot_util.h"
#include "peak_boot.h"

struct boot_elf_image {
    const uint8_t *data;
    size_t size;
};

struct boot_loaded_kernel {
    uint64_t entry_virt;
    uint64_t phys_base;
    uint64_t phys_size;
};

/* Validate ELF64 ET_EXEC/ET_DYN for x86_64 and load PT_LOAD segments.
 * map_page(virt, phys, writable) must install 4KiB mappings.
 * alloc_pages(n) returns physical address of n contiguous pages (zeroed OK).
 */
int boot_elf_load(const struct boot_elf_image *img,
                  uint64_t (*alloc_pages)(size_t n),
                  int (*map_page)(uint64_t virt, uint64_t phys, int writable),
                  struct boot_loaded_kernel *out);

#endif
