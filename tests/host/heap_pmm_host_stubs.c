/*
 * Host stubs for linking kernel/pmm.c + kernel/heap.c under PEAK_HOST_TEST.
 *
 * PMM tracks page frame numbers up to MAX_PAGES (~16 GiB), so we use a fake
 * physical window above the low-1MiB reserve and map it onto a host arena.
 */
#include "sync.h"
#include "vmm.h"
#include "pmm.h"
#include "util.h"
#include "peak_boot.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HOST_ARENA_PAGES 512u /* 2 MiB */
#define HOST_FAKE_PHYS   0x200000ull /* 2 MiB — above PMM's low-1MiB reserve */

static uint8_t *g_arena;
static size_t g_arena_bytes;
static uint64_t g_fake_phys;

void spin_init(struct spinlock *lk, const char *name) {
    if (!lk)
        return;
    lk->locked = 0;
    lk->name = name;
}

void spin_lock(struct spinlock *lk) {
    if (lk)
        lk->locked = 1;
}

void spin_unlock(struct spinlock *lk) {
    if (lk)
        lk->locked = 0;
}

void *vmm_phys_to_virt(uint64_t phys) {
    if (!g_arena || phys < g_fake_phys)
        return NULL;
    uint64_t off = phys - g_fake_phys;
    if (off >= g_arena_bytes)
        return NULL;
    return g_arena + off;
}

uint64_t vmm_virt_to_phys(void *virt) {
    uint8_t *p = (uint8_t *)virt;
    if (!g_arena || p < g_arena || p >= g_arena + g_arena_bytes)
        return 0;
    return g_fake_phys + (uint64_t)(p - g_arena);
}

int heap_pmm_host_setup(void) {
    g_arena_bytes = (size_t)HOST_ARENA_PAGES * 4096u;
    free(g_arena);
    g_arena = NULL;
    g_fake_phys = HOST_FAKE_PHYS;

    void *raw = NULL;
    if (posix_memalign(&raw, 4096, g_arena_bytes) != 0)
        return -1;
    g_arena = raw;
    memset(g_arena, 0, g_arena_bytes);

    struct peak_bootinfo info;
    memset(&info, 0, sizeof(info));
    info.hhdm_offset = 0;
    info.mmap_count = 1;
    info.mmap[0].base = g_fake_phys;
    info.mmap[0].length = g_arena_bytes;
    info.mmap[0].type = PEAK_MMAP_USABLE;
    pmm_init(&info);
    return 0;
}

void heap_pmm_host_teardown(void) {
    free(g_arena);
    g_arena = NULL;
    g_arena_bytes = 0;
    g_fake_phys = 0;
}
