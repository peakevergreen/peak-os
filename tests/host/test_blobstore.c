/*
 * Host tests for kernel/blobstore.c — create/read/write, LRU evict, sync/load.
 * Built with -DBLOBSTORE_CACHE_PAGES=4 so thrash is cheap to assert.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "blobstore.h"
#include "blockdev.h"

void blobstore_host_disk_reset(void);

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    expect(BLOBSTORE_CACHE_PAGES == 4u, "host build uses 4-page cache knob");

    blobstore_host_disk_reset();
    blobstore_init();
    expect(blobstore_available(), "available after init on mem disk");
    expect(blobstore_object_count() == 0, "fresh store empty");

    uint32_t id = 0;
    expect(blobstore_create(&id, 100) == 0, "create small object");
    expect(id != 0, "non-zero id");
    expect(blobstore_size(id) == 100, "reported size");

    char payload[] = "hello-blobstore";
    expect(blobstore_write(id, 0, payload, sizeof(payload)) == (int)sizeof(payload),
           "write payload");
    char got[32];
    memset(got, 0, sizeof(got));
    expect(blobstore_read(id, 0, got, sizeof(payload)) == (int)sizeof(payload),
           "read payload");
    expect(memcmp(got, payload, sizeof(payload)) == 0, "round-trip bytes");

    /* Fill more than CACHE_PAGES distinct pages to force eviction.
     * One object spanning CACHE_PAGES+2 pages; touch each page then re-read first. */
    uint32_t big = 0;
    size_t big_sz = (size_t)(BLOBSTORE_CACHE_PAGES + 2) * BLOBSTORE_PAGE_SIZE;
    expect(blobstore_create(&big, big_sz) == 0, "create multi-page object");
    for (uint32_t p = 0; p < BLOBSTORE_CACHE_PAGES + 2; p++) {
        uint8_t mark = (uint8_t)(0xA0 + p);
        expect(blobstore_write(big, (size_t)p * BLOBSTORE_PAGE_SIZE, &mark, 1) == 1,
               "write page mark");
    }
    expect(blobstore_cache_pages_used() <= BLOBSTORE_CACHE_PAGES,
           "cache never exceeds knob");
    /* Re-read first page after thrash — must come from disk after eviction. */
    uint8_t first = 0;
    expect(blobstore_read(big, 0, &first, 1) == 1, "read first page after thrash");
    expect(first == 0xA0, "first page survives eviction");

    expect(blobstore_sync() == 0, "sync meta + dirty pages");
    uint32_t before_count = blobstore_object_count();

    /* Re-init from same disk image (simulates load after reboot). */
    blobstore_init();
    expect(blobstore_available(), "available after reload");
    expect(blobstore_object_count() == before_count, "object count after load");
    memset(got, 0, sizeof(got));
    expect(blobstore_read(id, 0, got, sizeof(payload)) == (int)sizeof(payload),
           "read after reload");
    expect(memcmp(got, payload, sizeof(payload)) == 0, "payload persists across load");

    expect(blobstore_delete(id) == 0, "delete object");
    expect(blobstore_size(id) == 0, "deleted size zero");

    if (fails) {
        fprintf(stderr, "%d test(s) failed\n", fails);
        return 1;
    }
    printf("test_blobstore: ok\n");
    return 0;
}
