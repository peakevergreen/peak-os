/*
 * Host stubs for linking kernel/vfs.c + vfs_peakfs.c + blobstore.c under
 * PEAK_HOST_TEST. Heap-backed VFS is default; call vfs_host_blob_reset() to
 * enable the in-memory blockdev + blobstore for large-file tests.
 */
#include "heap.h"
#include "privacy.h"
#include "blobstore.h"
#include "blockdev.h"
#include "cap.h"

#include <stdlib.h>
#include <string.h>

#define HOST_BD_SECTORS 65536u

static uint8_t *g_disk;

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

void vfs_host_blob_reset(void) {
    free(g_disk);
    g_disk = calloc(1, (size_t)HOST_BD_SECTORS * BLOCKDEV_SECTOR_SIZE);
    blockdev_register(&host_bd);
    blobstore_init();
}

static int g_persist_profile = 2; /* full — matches default session */

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

void heap_init(void) {}

void heap_get_stats(uint64_t *used_bytes, uint64_t *free_bytes, uint64_t *blocks_out) {
    if (used_bytes)
        *used_bytes = 0;
    if (free_bytes)
        *free_bytes = 0;
    if (blocks_out)
        *blocks_out = 0;
}

uint64_t heap_total_allocated(void) {
    return 0;
}

void privacy_init(void) {
    g_persist_profile = 2;
}

void privacy_clear_session(void) {
    g_persist_profile = 2;
}

int privacy_persist_profile(void) {
    return g_persist_profile;
}

void privacy_set_persist_profile(int profile) {
    if (profile < 0)
        profile = 0;
    if (profile > 2)
        profile = 2;
    g_persist_profile = profile;
}

int privacy_net_kill_switch(void) {
    return 0;
}

void privacy_set_net_kill_switch(int on) {
    (void)on;
}

int privacy_net_client_allowed(void) {
    return 1;
}

void privacy_grant_net_client(int remember) {
    (void)remember;
}

int privacy_net_listen_allowed(int lan) {
    (void)lan;
    return 1;
}

void privacy_grant_net_listen(int lan, int remember) {
    (void)lan;
    (void)remember;
}

int privacy_listeners_localhost_only(void) {
    return 0;
}

void privacy_set_listeners_localhost_only(int on) {
    (void)on;
}

