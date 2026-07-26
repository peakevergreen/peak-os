#ifndef PEAK_VFS_H
#define PEAK_VFS_H

#include "types.h"

#define VFS_NAME_MAX 64
#define VFS_PATH_MAX 256
#define VFS_MAX_NODES 4096
#define VFS_MODE_FILE 0644u
#define VFS_MODE_DIR  0755u
#define VFS_SYMLINK_LOOP_MAX 8

enum vfs_type {
    VFS_DIR = 1,
    VFS_FILE = 2,
    VFS_SYMLINK = 3,
};

struct vfs_node {
    char name[VFS_NAME_MAX];
    enum vfs_type type;
    uint16_t mode;
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint32_t refs; /* hard-link refcount for files; dirs = 1 */
    /* 0 = heap-resident data[]; else blobstore object for large-file backend. */
    uint32_t blob_id;
    struct vfs_node *parent;
    struct vfs_node *child;
    struct vfs_node *sibling;
};

struct vfs_stat {
    enum vfs_type type;
    uint16_t mode;
    size_t size;
    uint32_t nchildren;
    uint32_t refs;
    char name[VFS_NAME_MAX];
    char link_target[VFS_PATH_MAX];
};

struct vfs_dirent {
    char name[VFS_NAME_MAX];
    enum vfs_type type;
};

typedef int (*vfs_walk_cb)(const char *path, struct vfs_node *node, void *ctx);

void vfs_init(void);
struct vfs_node *vfs_root(void);
struct vfs_node *vfs_lookup(const char *path);
struct vfs_node *vfs_mkdir(const char *path);
struct vfs_node *vfs_create_file(const char *path);
int vfs_write_file(const char *path, const void *data, size_t len);
int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len);
/* Bind an existing blobstore object as the file body (large-file prep). */
int vfs_bind_blob(const char *path, uint32_t blob_id, size_t size);
/* Allocate a blob object and bind it as a VFS file (returns PEAK_ENOENT if no disk). */
int vfs_create_blob_file(const char *path, size_t size);
/* Ranged I/O for audit tails and blob-backed large files. */
int vfs_read_at(const char *path, size_t off, void *buf, size_t len, size_t *out_len);
int vfs_write_at(const char *path, size_t off, const void *buf, size_t len);
int vfs_list(const char *path, char *out, size_t out_len);
int vfs_load_ramdisk(const void *blob, size_t len);
#define VFS_EXPORT_MAX_BYTES (32u * 1024u * 1024u)
#define VFS_EXPORT_STREAM_CHUNK 4096u

typedef int (*vfs_export_write_fn)(const void *data, size_t len, void *ctx);

/* Stream PEAKFS1 export (chunked blob reads; no full-file materialize). */
int vfs_export_ramdisk_stream(vfs_export_write_fn fn, void *ctx, uint32_t *out_bytes,
                              uint32_t *out_count);
const char *vfs_export_last_error(void);

/* Serialize files under / into PEAKFS1 blob. Returns bytes written or -1. */
int vfs_export_ramdisk(void *blob, size_t cap);
/* Bytes needed for a full PEAKFS1 export (including header), or -1. */
int vfs_export_ramdisk_size(void);
void vfs_seed_defaults(void);

int vfs_normalize(const char *path, char *out, size_t out_len);
int vfs_resolve(const char *path, char *out, size_t out_len);
int vfs_stat(const char *path, struct vfs_stat *st);
int vfs_chmod(const char *path, uint16_t mode);
void vfs_mode_string(enum vfs_type type, uint16_t mode, char *buf, size_t len);
int vfs_readlink(const char *path, char *buf, size_t buf_len, size_t *out_len);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_exists(const char *path);
int vfs_is_dir(const char *path);
int vfs_is_file(const char *path);
int vfs_is_symlink(const char *path);
int vfs_unlink(const char *path);
int vfs_rmdir(const char *path);
int vfs_remove_tree(const char *path);
int vfs_rename(const char *oldp, const char *newp);
int vfs_copy_file(const char *src, const char *dst);
int vfs_copy_file_ex(const char *src, const char *dst, int promote_blob);
int vfs_copy_tree(const char *src, const char *dst);
int vfs_copy_tree_ex(const char *src, const char *dst, int promote_blob);
int vfs_link(const char *target, const char *linkname);
int vfs_walk(const char *path, vfs_walk_cb cb, void *ctx);
int vfs_readdir(const char *path, struct vfs_dirent *ents, int max_ents);
int vfs_node_count(void);
uint64_t vfs_tree_bytes(const char *path);

/* Last VFS error detail string (empty if none). Not thread-safe. */
const char *vfs_last_error(void);
const char *vfs_last_error_path(void);

#endif
