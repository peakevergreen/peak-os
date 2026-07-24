#include "pmm.h"
#include "util.h"

#define PAGE_SIZE 4096ULL
#define MAX_PAGES (4096ULL * 1024ULL) /* support up to 16 GiB */
#define PAGES_PER_BLOCK 64u
#define MAX_BLOCKS (MAX_PAGES / PAGES_PER_BLOCK)
#define MAX_MIXED 4096
#define PMM_FREE_RUNS 128
#define PMM_SINGLE_STACK 2048

/*
 * Two-level page map (replaces a flat MAX_PAGES/8 = 512 KiB bitmap):
 *   - block_state: UNUSED / FREE (all 64 free) / USED (all 64 used) / MIXED
 *   - mixed_bits[slot]: 1 bit per page within a MIXED block (1 = used)
 *
 * Uniform FREE/USED blocks need no fine bits. When the mixed table is full,
 * splitting a FREE block spills leftover pages onto the freelist so alloc
 * cannot fail for lack of a mixed slot.
 *
 * BSS ≈ 64 KiB (state) + 128 KiB (slot index) + 32 KiB (mixed bits) = 224 KiB
 * versus 512 KiB for a flat bitmap — about 288 KiB saved, 16 GiB reach kept.
 */
enum {
    BLK_UNUSED = 0, /* never appeared as usable RAM */
    BLK_FREE = 1,   /* all 64 pages free */
    BLK_USED = 2,   /* all 64 pages used / reserved */
    BLK_MIXED = 3
};

static uint8_t block_state[MAX_BLOCKS];
static uint16_t block_mixed[MAX_BLOCKS]; /* valid only when BLK_MIXED */
static uint64_t mixed_bits[MAX_MIXED];   /* bit set => page used */
static uint16_t mixed_free_stack[MAX_MIXED];
static size_t mixed_free_n;

static uint64_t total_pages;
static uint64_t used_pages;
static uint64_t hhdm_offset;
static uint64_t highest_page;
static uint64_t alloc_cursor;

/*
 * Freelist pages stay marked used in the page map so the linear scanner
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

static int mixed_acquire(uint32_t blk, uint64_t initial_bits) {
    if (mixed_free_n == 0)
        return -1;
    uint16_t slot = mixed_free_stack[--mixed_free_n];
    mixed_bits[slot] = initial_bits;
    block_mixed[blk] = slot;
    block_state[blk] = BLK_MIXED;
    return (int)slot;
}

static void mixed_release_to(uint32_t blk, uint8_t new_state) {
    if (block_state[blk] == BLK_MIXED) {
        uint16_t slot = block_mixed[blk];
        if (mixed_free_n < MAX_MIXED)
            mixed_free_stack[mixed_free_n++] = slot;
        block_mixed[blk] = 0;
    }
    block_state[blk] = new_state;
}

static int page_is_used(uint64_t page) {
    if (page >= MAX_PAGES)
        return 1;
    uint32_t blk = (uint32_t)(page / PAGES_PER_BLOCK);
    uint32_t bit = (uint32_t)(page % PAGES_PER_BLOCK);
    uint8_t st = block_state[blk];
    if (st == BLK_UNUSED || st == BLK_USED)
        return 1;
    if (st == BLK_FREE)
        return 0;
    return (int)((mixed_bits[block_mixed[blk]] >> bit) & 1ull);
}

/* Spill leftover pages from a block onto the single freelist (map stays USED). */
static void spill_block_pages_except(uint32_t blk, uint32_t keep_bit) {
    for (uint32_t i = 0; i < PAGES_PER_BLOCK; i++) {
        if (i == keep_bit)
            continue;
        if (single_free_n >= PMM_SINGLE_STACK)
            break;
        single_free[single_free_n++] = (uint64_t)blk * PAGES_PER_BLOCK + i;
        if (used_pages)
            used_pages--;
    }
}

static void page_mark_used(uint64_t page) {
    if (page >= MAX_PAGES)
        return;
    uint32_t blk = (uint32_t)(page / PAGES_PER_BLOCK);
    uint32_t bit = (uint32_t)(page % PAGES_PER_BLOCK);
    uint8_t st = block_state[blk];
    if (st == BLK_USED || st == BLK_UNUSED)
        return;
    if (st == BLK_FREE) {
        if (mixed_acquire(blk, 1ull << bit) < 0) {
            block_state[blk] = BLK_USED;
            spill_block_pages_except(blk, bit);
        }
        return;
    }
    /* MIXED */
    uint16_t slot = block_mixed[blk];
    mixed_bits[slot] |= 1ull << bit;
    if (mixed_bits[slot] == ~0ull)
        mixed_release_to(blk, BLK_USED);
}

static void page_mark_free(uint64_t page) {
    if (page >= MAX_PAGES)
        return;
    uint32_t blk = (uint32_t)(page / PAGES_PER_BLOCK);
    uint32_t bit = (uint32_t)(page % PAGES_PER_BLOCK);
    uint8_t st = block_state[blk];
    if (st == BLK_FREE || st == BLK_UNUSED)
        return;
    if (st == BLK_USED) {
        if (mixed_acquire(blk, (~0ull) ^ (1ull << bit)) < 0) {
            if (single_free_n < PMM_SINGLE_STACK)
                single_free[single_free_n++] = page;
        }
        return;
    }
    /* MIXED */
    uint16_t slot = block_mixed[blk];
    mixed_bits[slot] &= ~(1ull << bit);
    if (mixed_bits[slot] == 0)
        mixed_release_to(blk, BLK_FREE);
}

/* Mark [start, end) page indices as usable/free. Full blocks become BLK_FREE. */
static void mark_usable_range(uint64_t start, uint64_t end) {
    if (end > MAX_PAGES)
        end = MAX_PAGES;
    while (start < end) {
        uint32_t bit = (uint32_t)(start % PAGES_PER_BLOCK);
        uint32_t blk = (uint32_t)(start / PAGES_PER_BLOCK);
        if (bit == 0 && start + PAGES_PER_BLOCK <= end) {
            if (block_state[blk] == BLK_MIXED)
                mixed_release_to(blk, BLK_FREE);
            else
                block_state[blk] = BLK_FREE;
            start += PAGES_PER_BLOCK;
            total_pages += PAGES_PER_BLOCK;
            highest_page = start - 1;
            continue;
        }
        /* Partial page within a block. */
        uint8_t st = block_state[blk];
        if (st == BLK_UNUSED) {
            if (mixed_acquire(blk, (~0ull) ^ (1ull << bit)) < 0) {
                /* Last resort: claim FREE and rely on reserve passes. */
                block_state[blk] = BLK_FREE;
            }
        } else if (st == BLK_USED) {
            page_mark_free(start);
        } else if (st == BLK_MIXED) {
            mixed_bits[block_mixed[blk]] &= ~(1ull << bit);
            if (mixed_bits[block_mixed[blk]] == 0)
                mixed_release_to(blk, BLK_FREE);
        }
        /* BLK_FREE: already free */
        total_pages++;
        if (start > highest_page)
            highest_page = start;
        start++;
    }
}

void pmm_init(struct peak_bootinfo *info) {
    hhdm_offset = info->hhdm_offset;
    memset(block_state, BLK_UNUSED, sizeof(block_state));
    memset(block_mixed, 0, sizeof(block_mixed));
    mixed_free_n = 0;
    for (uint16_t i = 0; i < MAX_MIXED; i++)
        mixed_free_stack[mixed_free_n++] = i;

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
        mark_usable_range(start, end);
    }

    /* Reserve page 0 and low 1MiB */
    for (uint64_t p = 0; p < 256 && p < MAX_PAGES; p++) {
        if (!page_is_used(p)) {
            page_mark_used(p);
            if (total_pages)
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
            if (!page_is_used(p)) {
                page_mark_used(p);
                if (total_pages)
                    total_pages--;
            }
        }
    }

    used_pages = 0;
    single_free_n = 0; /* drop any spill artifacts from reserve marks */
    free_run_n = 0;
    alloc_cursor = 256;
}

void *pmm_alloc(void) {
    return pmm_alloc_pages(1);
}

static void *alloc_from_map(size_t n) {
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
                if (page_is_used(start + i)) {
                    ok = 0;
                    start += i;
                    break;
                }
            }
            if (!ok)
                continue;
            for (size_t i = 0; i < n; i++) {
                page_mark_used(start + i);
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

    return alloc_from_map(n);
}

void pmm_free(void *phys) {
    pmm_free_n(phys, 1);
}

static int pmm_page_on_freelist(uint64_t page) {
    for (size_t i = 0; i < single_free_n; i++) {
        if (single_free[i] == page)
            return 1;
    }
    for (size_t i = 0; i < free_run_n; i++) {
        if (page >= free_runs[i].start &&
            page < free_runs[i].start + free_runs[i].n)
            return 1;
    }
    return 0;
}

void pmm_free_n(void *phys, size_t n) {
    uint64_t p = (uint64_t)phys / PAGE_SIZE;
    if (n == 0 || p >= MAX_PAGES)
        return;

    /* Freelist keeps map bits set — reject double-free before reuse. */
    for (size_t i = 0; i < n; i++) {
        if (p + i >= MAX_PAGES || pmm_page_on_freelist(p + i))
            return;
    }

    /* Prefer freelist reuse: keep map bits set so scan cannot steal. */
    if (n == 1 && single_free_n < PMM_SINGLE_STACK && page_is_used(p)) {
        single_free[single_free_n++] = p;
        if (used_pages)
            used_pages--;
        return;
    }

    if (n > 1 && free_run_n < PMM_FREE_RUNS) {
        int all_set = 1;
        for (size_t i = 0; i < n; i++) {
            if (p + i >= MAX_PAGES || !page_is_used(p + i)) {
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
        if (page_is_used(pg)) {
            page_mark_free(pg);
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
