/*
 * Host stubs for linking kernel/agent_policy.c, agent_tools.c, and
 * agent_planner.c under PEAK_HOST_TEST — in-memory VFS + PeakVec no-ops.
 */
#include "heap.h"
#include "vfs.h"
#include "peak_errno.h"
#include "peakvec.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HOST_VFS_FILES 48

struct host_file {
    int used;
    char path[VFS_PATH_MAX];
    char *data;
    size_t len;
};

static struct host_file g_files[HOST_VFS_FILES];

void agent_host_vfs_reset(void) {
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        free(g_files[i].data);
        memset(&g_files[i], 0, sizeof(g_files[i]));
    }
}

static struct host_file *host_find(const char *path) {
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        if (g_files[i].used && !strcmp(g_files[i].path, path))
            return &g_files[i];
    }
    return NULL;
}

static struct host_file *host_alloc(const char *path) {
    struct host_file *f = host_find(path);
    if (f)
        return f;
    for (int i = 0; i < HOST_VFS_FILES; i++) {
        if (!g_files[i].used) {
            size_t pl = strlen(path);
            if (pl + 1 > sizeof(g_files[i].path))
                return NULL;
            memcpy(g_files[i].path, path, pl + 1);
            g_files[i].used = 1;
            g_files[i].data = NULL;
            g_files[i].len = 0;
            return &g_files[i];
        }
    }
    return NULL;
}

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

int vfs_normalize(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len < 2 || path[0] != '/')
        return PEAK_EINVAL;
    char parts[32][VFS_NAME_MAX];
    int depth = 0;
    size_t i = 1;
    while (path[i] && depth < 32) {
        while (path[i] == '/')
            i++;
        if (!path[i])
            break;
        char part[VFS_NAME_MAX];
        size_t j = 0;
        while (path[i] && path[i] != '/' && j + 1 < VFS_NAME_MAX)
            part[j++] = path[i++];
        part[j] = '\0';
        if (!strcmp(part, ".") || j == 0)
            continue;
        if (!strcmp(part, "..")) {
            if (depth > 0)
                depth--;
            continue;
        }
        memcpy(parts[depth], part, j + 1);
        depth++;
    }
    if (depth == 0) {
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
    size_t o = 0;
    for (int p = 0; p < depth; p++) {
        if (o + 1 >= out_len)
            return PEAK_ENOSPC;
        out[o++] = '/';
        for (size_t k = 0; parts[p][k]; k++) {
            if (o + 1 >= out_len)
                return PEAK_ENOSPC;
            out[o++] = parts[p][k];
        }
    }
    out[o] = '\0';
    return 0;
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    struct host_file *f = host_alloc(path);
    if (!f)
        return PEAK_ENOSPC;
    char *buf = (char *)malloc(len ? len : 1);
    if (!buf)
        return PEAK_ENOMEM;
    if (len)
        memcpy(buf, data, len);
    free(f->data);
    f->data = buf;
    f->len = len;
    return 0;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    struct host_file *f = host_find(path);
    if (!f)
        return PEAK_ENOENT;
    size_t n = f->len < buf_len ? f->len : buf_len;
    if (n && buf)
        memcpy(buf, f->data, n);
    if (out_len)
        *out_len = n;
    return 0;
}

int vfs_stat(const char *path, struct vfs_stat *st) {
    struct host_file *f = host_find(path);
    if (!f || !st)
        return PEAK_ENOENT;
    st->type = VFS_FILE;
    st->size = f->len;
    st->refs = 1;
    st->nchildren = 0;
    st->name[0] = '\0';
    return 0;
}

int vfs_list(const char *path, char *out, size_t out_len) {
    if (!out || out_len < 2)
        return PEAK_EINVAL;
    (void)path;
    out[0] = '\0';
    return 0;
}

struct vfs_node *vfs_mkdir(const char *path) {
    (void)path;
    return NULL;
}

struct vfs_node *vfs_lookup(const char *path) {
    (void)path;
    return NULL;
}

int vfs_remove_tree(const char *path) {
    (void)path;
    return 0;
}

/* Write-approval stub: treat as auto-approved path for host tool tests. */
int agent_queue_write_approval(const char *path, const char *content) {
    (void)path;
    (void)content;
    return -1;
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

void peakvec_embed_text(const char *text, int16_t *out_vec) {
    (void)text;
    if (out_vec)
        memset(out_vec, 0, (size_t)PEAKVEC_DIM * sizeof(int16_t));
}

int peakvec_upsert(const char *ns, const char *key, const int16_t *vec, const char *meta) {
    (void)ns;
    (void)key;
    (void)vec;
    (void)meta;
    return 0;
}

int peakvec_query(const char *ns, const int16_t *query, int top_k, struct peakvec_hit *out) {
    (void)ns;
    (void)query;
    (void)top_k;
    (void)out;
    return 0;
}
