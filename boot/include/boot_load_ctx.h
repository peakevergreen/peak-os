#ifndef PEAK_BOOT_LOAD_CTX_H
#define PEAK_BOOT_LOAD_CTX_H

#include <stdint.h>
#include "boot_elf.h"
#include "boot_paging.h"
#include "boot_util.h"

/* Shared BIOS/UEFI arena allocator + paging context for boot_elf_load. */
struct boot_load_ctx {
    uint64_t pt_next;
    uint64_t pt_end;
    uint64_t pml4;
};

void boot_load_ctx_init(struct boot_load_ctx *ctx, uint64_t arena_start,
                        uint64_t arena_size);

uint64_t boot_load_ctx_alloc_pages(struct boot_load_ctx *ctx, size_t n);

int boot_load_ctx_map_page(struct boot_load_ctx *ctx, uint64_t virt,
                           uint64_t phys, int writable);

int boot_load_ctx_paging_init(struct boot_load_ctx *ctx, uint64_t identity_end);

uint64_t boot_load_ctx_pml4(const struct boot_load_ctx *ctx);

struct boot_page_allocator boot_load_ctx_page_alloc(struct boot_load_ctx *ctx);

int boot_load_ctx_elf_load(struct boot_load_ctx *ctx,
                           const struct boot_elf_image *img,
                           struct boot_loaded_kernel *out);

#endif
