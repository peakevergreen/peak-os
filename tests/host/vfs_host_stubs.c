/*
 * Host stubs for linking kernel/vfs.c + vfs_peakfs.c under PEAK_HOST_TEST.
 * Blobstore is unavailable — tests exercise heap-backed VFS paths only.
 */
#include "heap.h"
#include "privacy.h"
#include "blobstore.h"

#include <stdlib.h>
#include <string.h>

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

void blobstore_init(void) {}

int blobstore_available(void) {
    return 0;
}

int blobstore_create(uint32_t *out_id, size_t size) {
    (void)out_id;
    (void)size;
    return -1;
}

int blobstore_delete(uint32_t id) {
    (void)id;
    return -1;
}

int blobstore_resize(uint32_t id, size_t new_size) {
    (void)id;
    (void)new_size;
    return -1;
}

size_t blobstore_size(uint32_t id) {
    (void)id;
    return 0;
}

int blobstore_read(uint32_t id, size_t off, void *buf, size_t len) {
    (void)id;
    (void)off;
    (void)buf;
    (void)len;
    return -1;
}

int blobstore_write(uint32_t id, size_t off, const void *buf, size_t len) {
    (void)id;
    (void)off;
    (void)buf;
    (void)len;
    return -1;
}

int blobstore_sync(void) {
    return 0;
}

int blobstore_load(void) {
    return -1;
}

uint32_t blobstore_object_count(void) {
    return 0;
}

uint32_t blobstore_cache_pages_used(void) {
    return 0;
}

int blobstore_cached_at(uint32_t id, size_t off) {
    (void)id;
    (void)off;
    return 0;
}
