/*
 * Host stubs for linking kernel/peakvec.c under PEAK_HOST_TEST.
 * Heap/VFS/blobstore/cap are no-ops or fail-closed so in-memory PeakVec
 * logic can run without the full kernel.
 */
#include "heap.h"
#include "vfs.h"
#include "blobstore.h"
#include "cap.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

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

uint64_t heap_total_allocated(void) {
    return 0;
}

struct vfs_node *vfs_mkdir(const char *path) {
    (void)path;
    return NULL;
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    (void)path;
    (void)data;
    (void)len;
    return 0;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    (void)path;
    (void)buf;
    (void)buf_len;
    if (out_len)
        *out_len = 0;
    return -1;
}

int blobstore_available(void) {
    return 0;
}

int blobstore_create(uint32_t *out_id, size_t size) {
    (void)out_id;
    (void)size;
    return -1;
}

int blobstore_resize(uint32_t id, size_t new_size) {
    (void)id;
    (void)new_size;
    return -1;
}

int blobstore_write(uint32_t id, size_t off, const void *buf, size_t len) {
    (void)id;
    (void)off;
    (void)buf;
    (void)len;
    return -1;
}

int blobstore_read(uint32_t id, size_t off, void *buf, size_t len) {
    (void)id;
    (void)off;
    (void)buf;
    (void)len;
    return -1;
}

int cap_check(uint32_t need) {
    (void)need;
    return 1;
}

uint32_t sysmon_now_us(void) {
    static uint32_t t;
    return ++t;
}

void sysmon_note_peakvec_us(uint32_t us) {
    (void)us;
}

void sysmon_note_agent_audit_us(uint32_t us) {
    (void)us;
}

void itoa_u(uint64_t val, char *buf, int base) {
    if (base != 10 && base != 16)
        base = 10;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[32];
    int i = 0;
    while (val > 0) {
        int d = (int)(val % (uint64_t)base);
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        val /= (uint64_t)base;
    }
    int j = 0;
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = '\0';
}
