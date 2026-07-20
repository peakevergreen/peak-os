#include "pmm.h"
#include "util.h"

#define PAGE_SIZE 4096ULL
#define MAX_PAGES (1024ULL * 1024ULL) /* support up to 4 GiB bitmap */

static uint8_t bitmap[MAX_PAGES / 8];
static uint64_t total_pages;
static uint64_t used_pages;
static uint64_t hhdm_offset;
static uint64_t highest_page;

static void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (uint8_t)(1u << (page % 8));
}

static void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= (uint8_t)~(1u << (page % 8));
}

static int bitmap_test(uint64_t page) {
    return bitmap[page / 8] & (1u << (page % 8));
}

void pmm_init(struct limine_memmap_response *mmap, uint64_t hhdm) {
    hhdm_offset = hhdm;
    memset(bitmap, 0xFF, sizeof(bitmap)); /* mark all used */
    highest_page = 0;
    total_pages = 0;
    used_pages = 0;

    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *e = mmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;
        uint64_t start = (e->base + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t end = (e->base + e->length) / PAGE_SIZE;
        for (uint64_t p = start; p < end && p < MAX_PAGES; p++) {
            bitmap_clear(p);
            total_pages++;
            if (p > highest_page)
                highest_page = p;
        }
    }

    /* Reserve page 0 and low 1MiB */
    for (uint64_t p = 0; p < 256 && p < MAX_PAGES; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            total_pages--;
        }
    }

    used_pages = 0;
}

void *pmm_alloc(void) {
    for (uint64_t p = 0; p <= highest_page && p < MAX_PAGES; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            used_pages++;
            return (void *)(p * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free(void *phys) {
    uint64_t p = (uint64_t)phys / PAGE_SIZE;
    if (p >= MAX_PAGES)
        return;
    if (bitmap_test(p)) {
        bitmap_clear(p);
        if (used_pages)
            used_pages--;
    }
}

uint64_t pmm_total_pages(void) {
    return total_pages;
}

uint64_t pmm_free_pages(void) {
    return total_pages > used_pages ? total_pages - used_pages : 0;
}

uint64_t pmm_hhdm(void) {
    return hhdm_offset;
}
