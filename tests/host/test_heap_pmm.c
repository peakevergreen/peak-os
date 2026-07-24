/*
 * Host invariants for kernel/pmm.c + kernel/heap.c.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "heap.h"
#include "pmm.h"

int heap_pmm_host_setup(void);
void heap_pmm_host_teardown(void);

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    expect(heap_pmm_host_setup() == 0, "host arena + pmm_init");
    expect(pmm_total_pages() > 0, "usable pages registered");
    uint64_t free0 = pmm_free_pages();
    expect(free0 > 0, "free pages after init");

    void *p1 = pmm_alloc();
    void *p2 = pmm_alloc();
    expect(p1 && p2 && p1 != p2, "pmm_alloc distinct pages");
    expect(pmm_free_pages() == free0 - 2, "used_pages tracks alloc");
    pmm_free(p1);
    pmm_free(p2);
    expect(pmm_free_pages() == free0, "free restores count");

    void *run = pmm_alloc_pages(4);
    expect(run != NULL, "contiguous 4-page alloc");
    pmm_free_n(run, 4);
    expect(pmm_free_pages() == free0, "free_n restores count");

    /* Double-free of same page must not inflate free_pages. */
    void *once = pmm_alloc();
    expect(once != NULL, "single alloc");
    pmm_free(once);
    uint64_t after_one_free = pmm_free_pages();
    pmm_free(once); /* second free — freelist/bitmap path must no-op */
    expect(pmm_free_pages() == after_one_free, "pmm double free is a no-op");
    expect(pmm_free_pages() <= pmm_total_pages(), "free never exceeds total");

    /* Contiguous free then re-alloc from run freelist. */
    void *run2 = pmm_alloc_pages(3);
    expect(run2 != NULL, "3-page alloc for run freelist");
    pmm_free_n(run2, 3);
    pmm_free_n(run2, 3); /* double free_n */
    expect(pmm_free_pages() == free0, "run freelist double free is a no-op");
    void *run3 = pmm_alloc_pages(3);
    expect(run3 != NULL, "realloc from free run");
    pmm_free_n(run3, 3);
    expect(pmm_free_pages() == free0, "run recycle restores count");

    /* Cross a 64-page block boundary (two-level map stress). */
    {
        void *pages[96];
        int got = 1;
        for (int i = 0; i < 96; i++) {
            pages[i] = pmm_alloc();
            if (!pages[i])
                got = 0;
        }
        expect(got, "alloc 96 singles across block boundary");
        for (int i = 0; i < 96; i++)
            pmm_free(pages[i]);
        expect(pmm_free_pages() == free0, "block-boundary frees restore count");
    }

    heap_init();
    void *a = kmalloc(32);
    void *b = kmalloc(64);
    expect(a && b && a != b, "kmalloc distinct");
    memset(a, 0xAB, 32);
    memset(b, 0xCD, 64);
    uint64_t used = 0, freeb = 0, nblocks = 0;
    heap_get_stats(&used, &freeb, &nblocks);
    expect(used >= 32 + 64, "stats reflect used bytes");
    expect(nblocks > 0, "heap has blocks");

    uint64_t used_after_free = 0, free_after = 0, blocks_after = 0;
    kfree(a);
    heap_get_stats(&used_after_free, &free_after, &blocks_after);
    kfree(a); /* double-free must be rejected (no second freelist push) */
    {
        uint64_t used2 = 0, free2 = 0, blocks2 = 0;
        heap_get_stats(&used2, &free2, &blocks2);
        expect(used2 == used_after_free && free2 == free_after &&
               blocks2 == blocks_after,
               "heap double free leaves stats unchanged");
    }
    kfree(b);

    void *c = kzalloc(128);
    expect(c != NULL, "kzalloc");
    {
        uint8_t *p = c;
        int zero = 1;
        for (int i = 0; i < 128; i++)
            if (p[i])
                zero = 0;
        expect(zero, "kzalloc zeroed");
    }
    void *d = krealloc(c, 256);
    expect(d != NULL, "krealloc grow");
    kfree(d);

    /* Large allocation path (> page payload). */
    void *big = kmalloc(8192);
    expect(big != NULL, "large kmalloc");
    kfree(big);

    /*
     * Free-list coalescing: adjacent frees merge so a larger request can
     * reuse the combined region without growing from PMM.
     */
    {
        uint64_t blocks_before = 0;
        heap_get_stats(NULL, NULL, &blocks_before);

        void *s0 = kmalloc(64);
        void *s1 = kmalloc(64);
        void *s2 = kmalloc(64);
        expect(s0 && s1 && s2, "small triad alloc");

        uint64_t blocks_split = 0;
        heap_get_stats(NULL, NULL, &blocks_split);
        expect(blocks_split > blocks_before, "splits increase block count");

        /* Forward coalesce: free earlier neighbor first, then later. */
        kfree(s0);
        kfree(s1);
        {
            uint64_t blocks_fwd = 0;
            heap_get_stats(NULL, NULL, &blocks_fwd);
            expect(blocks_fwd < blocks_split, "forward coalesce merges neighbors");
        }

        /* Backward coalesce: free later neighbor of an already-free block. */
        void *t0 = kmalloc(64);
        void *t1 = kmalloc(64);
        expect(t0 && t1, "pair for backward coalesce");
        uint64_t blocks_pair = 0;
        heap_get_stats(NULL, NULL, &blocks_pair);
        kfree(t1);
        kfree(t0); /* prev of free t1 — must merge via prev link */
        {
            uint64_t blocks_back = 0;
            heap_get_stats(NULL, NULL, &blocks_back);
            expect(blocks_back < blocks_pair, "backward coalesce merges neighbors");
        }

        /* Coalesced 64+64 region should satisfy a 128-byte alloc. */
        void *joined = kmalloc(128);
        expect(joined != NULL, "kmalloc reuses coalesced free space");
        kfree(joined);
        kfree(s2);

        uint64_t blocks_after = 0;
        heap_get_stats(NULL, NULL, &blocks_after);
        expect(blocks_after <= blocks_before + 2,
               "coalesce restores toward pre-split block count");
    }

    heap_pmm_host_teardown();

    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("test_heap_pmm: ok\n");
    return 0;
}
