#include "boot_load_ctx.h"
#include "boot_elf.h"

static struct boot_load_ctx *active_ctx;

static uint64_t arena_alloc_pages(size_t n) {
    return boot_load_ctx_alloc_pages(active_ctx, n);
}

static int arena_map_page(uint64_t virt, uint64_t phys, int writable) {
    if (!active_ctx || !active_ctx->pml4)
        return -1;
    struct boot_page_allocator a = { .alloc_pages = arena_alloc_pages };
    return boot_map_page(active_ctx->pml4, virt, phys, writable, &a);
}

void boot_load_ctx_init(struct boot_load_ctx *ctx, uint64_t arena_start,
                        uint64_t arena_size) {
    if (!ctx)
        return;
    ctx->pt_next = arena_start;
    ctx->pt_end = arena_start + arena_size;
    ctx->pml4 = 0;
    active_ctx = ctx;
}

uint64_t boot_load_ctx_alloc_pages(struct boot_load_ctx *ctx, size_t n) {
    if (!ctx)
        return 0;
    uint64_t need = (uint64_t)n * BOOT_PAGE_SIZE;
    if (ctx->pt_next + need > ctx->pt_end)
        return 0;
    uint64_t p = ctx->pt_next;
    ctx->pt_next += need;
    boot_memset((void *)(uintptr_t)p, 0, (size_t)need);
    return p;
}

int boot_load_ctx_map_page(struct boot_load_ctx *ctx, uint64_t virt,
                           uint64_t phys, int writable) {
    active_ctx = ctx;
    return arena_map_page(virt, phys, writable);
}

int boot_load_ctx_paging_init(struct boot_load_ctx *ctx, uint64_t identity_end) {
    if (!ctx)
        return -1;
    active_ctx = ctx;
    struct boot_page_allocator a = { .alloc_pages = arena_alloc_pages };
    return boot_paging_init(&a, identity_end, &ctx->pml4);
}

uint64_t boot_load_ctx_pml4(const struct boot_load_ctx *ctx) {
    return ctx ? ctx->pml4 : 0;
}

struct boot_page_allocator boot_load_ctx_page_alloc(struct boot_load_ctx *ctx) {
    active_ctx = ctx;
    return (struct boot_page_allocator){ .alloc_pages = arena_alloc_pages };
}

int boot_load_ctx_elf_load(struct boot_load_ctx *ctx,
                           const struct boot_elf_image *img,
                           struct boot_loaded_kernel *out) {
    if (!ctx || !img || !out)
        return -1;
    active_ctx = ctx;
    return boot_elf_load(img, arena_alloc_pages, arena_map_page, out);
}
