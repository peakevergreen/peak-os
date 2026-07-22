#include "pmm.h"
#include "util.h"

#define PAGE_SIZE 4096ULL
#define MAX_PAGES (4096ULL * 1024ULL) /* support up to 16 GiB bitmap */
#define PMM_FREE_RUNS 128
#define PMM_SINGLE_STACK 2048

static uint8_t bitmap[MAX_PAGES / 8];
static uint64_t total_pages;
static uint64_t used_pages;
static uint64_t hhdm_offset;
static uint64_t highest_page;
static uint64_t alloc_cursor;

/*
 * Freelist pages stay marked used in the bitmap so the linear scanner
 * cannot double-allocate them. used_pages is adjusted so free_pages()
 * still reflects reclaimable freelist stock.
 */
static uint64_t single_free[PMM_SINGLE_STACK];
static size_t single_free_n;

struct pmm_run {
    uint64_t start;
    size_t n;
};
static struct pmm_run free_runs[PMM_FREE_RUNS];
static size_t free_run_n;

static void bitmap_set(uint64_t page) {
    bitmap[page / 8] |= (uint8_t)(1u << (page % 8));
}

static void bitmap_clear(uint64_t page) {
    bitmap[page / 8] &= (uint8_t)~(1u << (page % 8));
}

static int bitmap_test(uint64_t page) {
    return bitmap[page / 8] & (1u << (page % 8));
}

void pmm_init(struct peak_bootinfo *info) {
    hhdm_offset = info->hhdm_offset;
    memset(bitmap, 0xFF, sizeof(bitmap)); /* mark all used */
    highest_page = 0;
    total_pages = 0;
    used_pages = 0;
    single_free_n = 0;
    free_run_n = 0;

    for (uint32_t i = 0; i < info->mmap_count; i++) {
        struct peak_mmap_entry *e = &info->mmap[i];
        if (e->type != PEAK_MMAP_USABLE)
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

    /* Reserve kernel, bootloader, framebuffer ranges from BootInfo */
    for (uint32_t i = 0; i < info->mmap_count; i++) {
        struct peak_mmap_entry *e = &info->mmap[i];
        if (e->type != PEAK_MMAP_KERNEL && e->type != PEAK_MMAP_BOOTLOADER &&
            e->type != PEAK_MMAP_FRAMEBUFFER)
            continue;
        uint64_t start = e->base / PAGE_SIZE;
        uint64_t end = (e->base + e->length + PAGE_SIZE - 1) / PAGE_SIZE;
        for (uint64_t p = start; p < end && p < MAX_PAGES; p++) {
            if (!bitmap_test(p)) {
                bitmap_set(p);
                if (total_pages)
                    total_pages--;
            }
        }
    }

    used_pages = 0;
    alloc_cursor = 256;
}

void *pmm_alloc(void) {
    return pmm_alloc_pages(1);
}

static void *alloc_from_bitmap(size_t n) {
    uint64_t limit = highest_page + 1;
    if (limit > MAX_PAGES)
        limit = MAX_PAGES;
    if (alloc_cursor >= limit)
        alloc_cursor = 256;
    for (int pass = 0; pass < 2; pass++) {
        uint64_t start = pass == 0 ? alloc_cursor : 256;
        uint64_t end = pass == 0 ? limit : alloc_cursor;
        for (; start + n <= end; start++) {
            int ok = 1;
            for (size_t i = 0; i < n; i++) {
                if (bitmap_test(start + i)) {
                    ok = 0;
                    start += i;
                    break;
                }
            }
            if (!ok)
                continue;
            for (size_t i = 0; i < n; i++) {
                bitmap_set(start + i);
                used_pages++;
            }
            alloc_cursor = start + n;
            return (void *)(start * PAGE_SIZE);
        }
    }
    return NULL;
}

void *pmm_alloc_pages(size_t n) {
    if (n == 0)
        return NULL;

    if (n == 1 && single_free_n > 0) {
        uint64_t pg = single_free[--single_free_n];
        used_pages++; /* was counted free while on freelist */
        return (void *)(pg * PAGE_SIZE);
    }

    if (n > 1 && free_run_n > 0) {
        for (size_t i = 0; i < free_run_n; i++) {
            if (free_runs[i].n < n)
                continue;
            uint64_t start = free_runs[i].start;
            size_t left = free_runs[i].n - n;
            if (left == 0) {
                free_runs[i] = free_runs[--free_run_n];
            } else if (left == 1 && single_free_n < PMM_SINGLE_STACK) {
                single_free[single_free_n++] = start + n;
                free_runs[i] = free_runs[--free_run_n];
            } else {
                free_runs[i].start = start + n;
                free_runs[i].n = left;
            }
            used_pages += n;
            return (void *)(start * PAGE_SIZE);
        }
    }

    return alloc_from_bitmap(n);
}

void pmm_free(void *phys) {
    pmm_free_n(phys, 1);
}

void pmm_free_n(void *phys, size_t n) {
    uint64_t p = (uint64_t)phys / PAGE_SIZE;
    if (n == 0 || p >= MAX_PAGES)
        return;

    /* Prefer freelist reuse: keep bitmap bits set so scan cannot steal. */
    if (n == 1 && single_free_n < PMM_SINGLE_STACK && bitmap_test(p)) {
        single_free[single_free_n++] = p;
        if (used_pages)
            used_pages--;
        return;
    }

    if (n > 1 && free_run_n < PMM_FREE_RUNS) {
        int all_set = 1;
        for (size_t i = 0; i < n; i++) {
            if (p + i >= MAX_PAGES || !bitmap_test(p + i)) {
                all_set = 0;
                break;
            }
        }
        if (all_set) {
            free_runs[free_run_n].start = p;
            free_runs[free_run_n].n = n;
            free_run_n++;
            used_pages = used_pages > n ? used_pages - n : 0;
            return;
        }
    }

    for (size_t i = 0; i < n; i++) {
        uint64_t pg = p + i;
        if (pg >= MAX_PAGES)
            break;
        if (bitmap_test(pg)) {
            bitmap_clear(pg);
            if (used_pages)
                used_pages--;
        }
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
