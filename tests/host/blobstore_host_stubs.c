/*
 * Host stubs for linking kernel/blobstore.c under PEAK_HOST_TEST.
 * In-memory blockdev covers PeakFS base + blob meta/data area.
 */
#include "heap.h"
#include "blockdev.h"
#include "cap.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* Enough LBAs for meta + a few MiB of blob pages. */
#define HOST_BD_SECTORS 65536u

static uint8_t *g_disk;
static size_t g_disk_bytes;

void *kmalloc(size_t size) {
    return malloc(size ? size : 1);
}

void *kzalloc(size_t size) {
    return calloc(1, size ? size : 1);
}

void *krealloc(void *ptr, size_t size) {
    return realloc(ptr, size ? size : 1);
}

void kfree(void *ptr) {
    free(ptr);
}

int cap_check(uint32_t need) {
    (void)need;
    return 1;
}

static int host_bd_present(void) {
    return g_disk != NULL;
}

static int host_bd_read(uint64_t lba, uint32_t count, void *buf) {
    if (!g_disk || lba + count > HOST_BD_SECTORS)
        return -1;
    memcpy(buf, g_disk + (size_t)lba * BLOCKDEV_SECTOR_SIZE,
           (size_t)count * BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static int host_bd_write(uint64_t lba, uint32_t count, const void *buf) {
    if (!g_disk || lba + count > HOST_BD_SECTORS)
        return -1;
    memcpy(g_disk + (size_t)lba * BLOCKDEV_SECTOR_SIZE, buf,
           (size_t)count * BLOCKDEV_SECTOR_SIZE);
    return 0;
}

static int host_bd_flush(void) {
    return 0;
}

static const struct blockdev_ops host_bd = {
    .name = "host-mem",
    .present = host_bd_present,
    .read = host_bd_read,
    .write = host_bd_write,
    .flush = host_bd_flush,
};

void blobstore_host_disk_reset(void) {
    g_disk_bytes = (size_t)HOST_BD_SECTORS * BLOCKDEV_SECTOR_SIZE;
    free(g_disk);
    g_disk = calloc(1, g_disk_bytes);
    blockdev_register(&host_bd);
}
