#include "vfs.h"
#include "peak_errno.h"
#include "heap.h"
#include "util.h"
#include "blobstore.h"
#include "peakdisk.h"

static struct vfs_node nodes[VFS_MAX_NODES];
static int node_count;
static struct vfs_node *root;
static char vfs_errbuf[80];
static char vfs_err_path[VFS_PATH_MAX];

const char *vfs_last_error(void) {
    return vfs_errbuf;
}

const char *vfs_last_error_path(void) {
    return vfs_err_path;
}

static void vfs_set_error(const char *msg) {
    size_t i = 0;
    if (!msg)
        msg = "";
    for (; msg[i] && i + 1 < sizeof(vfs_errbuf); i++)
        vfs_errbuf[i] = msg[i];
    vfs_errbuf[i] = '\0';
}

static void vfs_set_error_path(const char *path, const char *msg) {
    vfs_set_error(msg);
    if (!path)
        path = "";
    size_t i = 0;
    for (; path[i] && i + 1 < sizeof(vfs_err_path); i++)
        vfs_err_path[i] = path[i];
    vfs_err_path[i] = '\0';
}

static void vfs_note_workspace_dirty(void) {
    peakdisk_mark_dirty();
}
/* First-character child buckets: 0 = empty, else 1-based index into nodes[].
 * 16 buckets (was 32) halves BSS (~128 KiB) with the same first-char probe. */
#define VFS_CHILD_BUCKETS 8u
static uint16_t child_bucket[VFS_MAX_NODES][VFS_CHILD_BUCKETS];

static int node_index(struct vfs_node *n) {
    if (!n)
        return -1;
    intptr_t d = (intptr_t)(n - nodes);
    if (d < 0 || d >= VFS_MAX_NODES)
        return -1;
    return (int)d;
}

static unsigned name_bucket(const char *name) {
    return (unsigned)(unsigned char)name[0] & (VFS_CHILD_BUCKETS - 1u);
}

static struct vfs_node *alloc_node(const char *name, enum vfs_type type) {
    struct vfs_node *n = NULL;
    /* Reuse freed slots (type == 0) before growing the table. */
    for (int i = 0; i < node_count; i++) {
        if (nodes[i].type == 0) {
            n = &nodes[i];
            break;
        }
    }
    if (!n) {
        if (node_count >= VFS_MAX_NODES)
            return NULL;
        n = &nodes[node_count++];
    }
    memset(n, 0, sizeof(*n));
    size_t i = 0;
    for (; name[i] && i + 1 < VFS_NAME_MAX; i++)
        n->name[i] = name[i];
    n->name[i] = '\0';
    n->type = type;
    n->mode = (type == VFS_DIR) ? VFS_MODE_DIR : VFS_MODE_FILE;
    n->refs = 1;
    return n;
}

static void add_child(struct vfs_node *parent, struct vfs_node *child) {
    child->parent = parent;
    child->sibling = parent->child;
    parent->child = child;
    int pi = node_index(parent);
    int ci = node_index(child);
    if (pi >= 0 && ci >= 0) {
        unsigned b = name_bucket(child->name);
        /* Prepend keeps this child at the front of the sibling chain. */
        child_bucket[pi][b] = (uint16_t)(ci + 1);
    }
}

void vfs_init(void) {
    memset(nodes, 0, sizeof(nodes));
    memset(child_bucket, 0, sizeof(child_bucket));
    node_count = 0;
    root = alloc_node("", VFS_DIR);
}

struct vfs_node *vfs_root(void) {
    return root;
}

static struct vfs_node *find_child(struct vfs_node *dir, const char *name) {
    if (!dir || !name)
        return NULL;
    int di = node_index(dir);
    unsigned b = name_bucket(name);
    struct vfs_node *start = dir->child;
    if (di >= 0 && child_bucket[di][b]) {
        uint16_t idx = child_bucket[di][b];
        if (idx >= 1 && idx <= (uint16_t)VFS_MAX_NODES)
            start = &nodes[idx - 1];
    }
    for (struct vfs_node *c = start; c; c = c->sibling) {
        if (((unsigned)(unsigned char)c->name[0] & (VFS_CHILD_BUCKETS - 1u)) != b)
            continue;
        if (!strcmp(c->name, name))
            return c;
    }
    /* If we started mid-chain from a stale bucket head, scan from the real head. */
    if (start != dir->child) {
        for (struct vfs_node *c = dir->child; c && c != start; c = c->sibling) {
            if (((unsigned)(unsigned char)c->name[0] & (VFS_CHILD_BUCKETS - 1u)) != b)
                continue;
            if (!strcmp(c->name, name))
                return c;
        }
    }
    return NULL;
}

static int path_join1(const char *base, const char *part, char *out, size_t out_len) {
    char tmp[VFS_PATH_MAX]; size_t bl = strlen(base); size_t o = 0;
    for (size_t k = 0; k < bl && o + 1 < sizeof(tmp); k++) tmp[o++] = base[k];
    if (o + 1 + strlen(part) >= sizeof(tmp)) return PEAK_ENOSPC;
    if (!(o == 1 && tmp[0] == '/')) tmp[o++] = '/';
    for (size_t k = 0; part[k]; k++) tmp[o++] = part[k];
    tmp[o] = '\0'; return vfs_normalize(tmp, out, out_len);
}
static int path_dirname(const char *path, char *out, size_t out_len) {
    char norm[VFS_PATH_MAX]; if (vfs_normalize(path, norm, sizeof(norm)) != 0) return PEAK_EINVAL;
    size_t len = strlen(norm); if (len <= 1) { out[0] = '/'; out[1] = '\0'; return 0; }
    int last = -1; for (size_t i = 0; norm[i]; i++) if (norm[i] == '/') last = (int)i;
    if (last <= 0) { out[0] = '/'; out[1] = '\0'; return 0; }
    if ((size_t)last + 1 > out_len) return PEAK_ENOSPC;
    memcpy(out, norm, (size_t)last); out[last] = '\0'; return 0;
}
static int expand_symlink_at(const char *link_path, struct vfs_node *link, const char *remainder, char *out, size_t out_len) {
    if (!link || link->type != VFS_SYMLINK || !link->data) return PEAK_EINVAL;
    const char *target = (const char *)link->data; char combined[VFS_PATH_MAX]; size_t o = 0;
    if (target[0] == '/') { for (; target[o] && o + 1 < sizeof(combined); o++) combined[o] = target[o]; }
    else {
        char dir[VFS_PATH_MAX]; if (path_dirname(link_path, dir, sizeof(dir)) != 0) return PEAK_ENOSPC;
        for (size_t k = 0; dir[k] && o + 1 < sizeof(combined); k++) combined[o++] = dir[k];
        if (o + 1 >= sizeof(combined)) return PEAK_ENOSPC; combined[o++] = '/';
        for (size_t k = 0; target[k] && o + 1 < sizeof(combined); k++) combined[o++] = target[k];
    }
    if (remainder && remainder[0]) {
        if (remainder[0] != '/') { if (o + 1 >= sizeof(combined)) return PEAK_ENOSPC; combined[o++] = '/'; }
        for (size_t k = 0; remainder[k] && o + 1 < sizeof(combined); k++) combined[o++] = remainder[k];
    }
    combined[o] = '\0'; return vfs_normalize(combined, out, out_len);
}
int vfs_resolve(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len < 2) return PEAK_EINVAL;
    char work[VFS_PATH_MAX]; int rc = vfs_normalize(path, work, sizeof(work)); if (rc != 0) return rc;
    int depth = 0;
resolve_restart: {
    struct vfs_node *cur = root; size_t i = 1; char link_path[VFS_PATH_MAX]; link_path[0] = '/'; link_path[1] = '\0';
    while (work[i]) {
        char part[VFS_NAME_MAX]; size_t j = 0;
        while (work[i] && work[i] != '/' && j + 1 < VFS_NAME_MAX) part[j++] = work[i++];
        part[j] = '\0'; if (work[i] == '/') i++; if (j == 0) continue;
        rc = path_join1(link_path, part, link_path, sizeof(link_path)); if (rc != 0) return rc;
        cur = find_child(cur, part); if (!cur) { vfs_set_error("not found"); return PEAK_ENOENT; }
        if (cur->type == VFS_SYMLINK) {
            if (++depth > VFS_SYMLINK_LOOP_MAX) {
                char emsg[96];
                snprintf(emsg, sizeof(emsg), "symlink loop at %s (max %d)",
                         link_path, VFS_SYMLINK_LOOP_MAX);
                vfs_set_error_path(link_path, emsg);
                return PEAK_ELOOP;
            }
            const char *rest = work[i] ? work + i : "";
            rc = expand_symlink_at(link_path, cur, rest, work, sizeof(work)); if (rc != 0) return rc;
            goto resolve_restart;
        }
        if (cur->type != VFS_DIR && work[i]) { vfs_set_error("not a directory"); return PEAK_ENOTDIR; }
    }}
    size_t wl = strlen(work); if (wl + 1 > out_len) return PEAK_ENOSPC;
    memcpy(out, work, wl + 1); vfs_set_error(""); return 0;
}
static int path_for_io(const char *path, char *out, size_t out_len) {
    int rc = vfs_resolve(path, out, out_len); if (rc == PEAK_ENOENT) rc = vfs_normalize(path, out, out_len); return rc;
}
struct vfs_node *vfs_lookup(const char *path) {
    if (!path || path[0] != '/')
        return NULL;
    if (path[1] == '\0')
        return root;

    struct vfs_node *cur = root;
    char part[VFS_NAME_MAX];
    size_t i = 1;
    while (path[i]) {
        size_t j = 0;
        while (path[i] && path[i] != '/' && j + 1 < VFS_NAME_MAX)
            part[j++] = path[i++];
        part[j] = '\0';
        if (path[i] == '/')
            i++;
        if (j == 0)
            continue;
        cur = find_child(cur, part);
        if (!cur)
            return NULL;
    }
    return cur;
}

struct vfs_node *vfs_mkdir(const char *path) {
    struct vfs_node *existing = vfs_lookup(path);
    if (existing) {
        if (existing->type == VFS_DIR)
            return existing;
        return NULL;
    }
    /* recursively create */
    char build[VFS_PATH_MAX];
    build[0] = '\0';
    size_t bi = 0;
    if (path[0] != '/')
        return NULL;
    size_t i = 1;
    struct vfs_node *cur = root;
    while (path[i]) {
        char part[VFS_NAME_MAX];
        size_t j = 0;
        while (path[i] && path[i] != '/' && j + 1 < VFS_NAME_MAX)
            part[j++] = path[i++];
        part[j] = '\0';
        if (path[i] == '/')
            i++;
        if (!j)
            continue;
        build[bi++] = '/';
        for (size_t k = 0; part[k] && bi + 1 < VFS_PATH_MAX; k++)
            build[bi++] = part[k];
        build[bi] = '\0';
        struct vfs_node *n = find_child(cur, part);
        if (n) {
            if (n->type != VFS_DIR)
                return NULL;
        } else {
            n = alloc_node(part, VFS_DIR);
            if (!n)
                return NULL;
            add_child(cur, n);
        }
        cur = n;
    }
    return cur;
}

struct vfs_node *vfs_create_file(const char *path) {
    struct vfs_node *existing = vfs_lookup(path);
    if (existing)
        return existing->type == VFS_FILE ? existing : NULL;

    /* ensure parent dirs */
    char parent[VFS_PATH_MAX];
    size_t n = 0;
    while (path[n] && n + 1 < VFS_PATH_MAX) {
        parent[n] = path[n];
        n++;
    }
    parent[n] = '\0';
    int last = -1;
    for (size_t i = 0; parent[i]; i++)
        if (parent[i] == '/')
            last = (int)i;
    if (last < 0)
        return NULL;
    parent[last] = '\0';
    const char *leaf = path + last + 1;
    if (last == 0) {
        /* parent is root */
    } else {
        if (!vfs_mkdir(parent))
            return NULL;
    }
    struct vfs_node *dir = (last == 0) ? root : vfs_lookup(parent);
    if (!dir || dir->type != VFS_DIR)
        return NULL;
    struct vfs_node *f = alloc_node(leaf, VFS_FILE);
    if (!f)
        return NULL;
    add_child(dir, f);
    return f;
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    char resolved[VFS_PATH_MAX];
    int pr = path_for_io(path, resolved, sizeof(resolved));
    if (pr != 0) return pr;
    path = resolved;
    struct vfs_node *existing = vfs_lookup(path);
    if (existing && existing->type == VFS_DIR) {
        vfs_set_error("is a directory");
        return PEAK_EISDIR;
    }
    struct vfs_node *f = vfs_create_file(path);
    if (!f || f->type != VFS_FILE) {
        vfs_set_error("cannot create file");
        return PEAK_EINVAL;
    }
    uint8_t *buf = (uint8_t *)kmalloc(len ? len : 1);
    if (!buf) {
        vfs_set_error("out of memory");
        return PEAK_ENOMEM;
    }
    if (f->data)
        kfree(f->data);
    memcpy(buf, data, len);
    f->data = buf;
    f->size = len;
    f->capacity = len;
    f->blob_id = 0; /* heap wins over any prior blob binding */
    vfs_set_error("");
    vfs_note_workspace_dirty();
    return 0;
}

int vfs_bind_blob(const char *path, uint32_t blob_id, size_t size) {
    struct vfs_node *f = vfs_create_file(path);
    if (!f || f->type != VFS_FILE || !blob_id)
        return PEAK_EINVAL;
    if (!blobstore_available())
        return PEAK_ENOENT;
    if (f->data) {
        kfree(f->data);
        f->data = NULL;
    }
    f->capacity = 0;
    f->blob_id = blob_id;
    f->size = size;
    vfs_note_workspace_dirty();
    return 0;
}

int vfs_create_blob_file(const char *path, size_t size) {
    if (!blobstore_available())
        return PEAK_ENOENT;
    uint32_t id = 0;
    if (blobstore_create(&id, size) != 0)
        return PEAK_ENOENT;
    return vfs_bind_blob(path, id, size);
}

int vfs_read_at(const char *path, size_t off, void *buf, size_t len, size_t *out_len) {
    char resolved[VFS_PATH_MAX];
    int pr = path_for_io(path, resolved, sizeof(resolved));
    if (pr != 0) return pr;
    path = resolved;
    struct vfs_node *f = vfs_lookup(path);
    if (!f || f->type != VFS_FILE)
        return PEAK_ENOENT;
    if (!buf)
        return PEAK_EINVAL;
    if (off >= f->size) {
        if (out_len)
            *out_len = 0;
        return 0;
    }
    if (off + len > f->size)
        len = f->size - off;
    if (f->blob_id) {
        int n = blobstore_read(f->blob_id, off, buf, len);
        if (n < 0)
            return PEAK_EIO;
        if (out_len)
            *out_len = (size_t)n;
        return 0;
    }
    if (!f->data && len > 0)
        return PEAK_EIO;
    memcpy(buf, f->data + off, len);
    if (out_len)
        *out_len = len;
    return 0;
}

int vfs_write_at(const char *path, size_t off, const void *buf, size_t len) {
    char resolved[VFS_PATH_MAX];
    int pr = path_for_io(path, resolved, sizeof(resolved));
    if (pr != 0) return pr;
    path = resolved;
    struct vfs_node *f = vfs_lookup(path);
    if (!f || f->type != VFS_FILE)
        return PEAK_ENOENT;
    if (!buf)
        return PEAK_EINVAL;
    if (f->blob_id) {
        int n = blobstore_write(f->blob_id, off, buf, len);
        if (n < 0)
            return PEAK_EIO;
        if (off + (size_t)n > f->size)
            f->size = off + (size_t)n;
        vfs_note_workspace_dirty();
        return 0;
    }
    /* Heap-backed ranged write: grow via full replace for small files only. */
    size_t need = off + len;
    if (need > f->capacity || !f->data) {
        uint8_t *nbuf = (uint8_t *)kmalloc(need ? need : 1);
        if (!nbuf)
            return PEAK_ENOMEM;
        if (f->data && f->size)
            memcpy(nbuf, f->data, f->size);
        if (f->size < off)
            memset(nbuf + f->size, 0, off - f->size);
        if (f->data)
            kfree(f->data);
        f->data = nbuf;
        f->capacity = need;
    }
    memcpy(f->data + off, buf, len);
    if (need > f->size)
        f->size = need;
    vfs_note_workspace_dirty();
    return 0;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    return vfs_read_at(path, 0, buf, buf_len, out_len);
}

int vfs_list(const char *path, char *out, size_t out_len) {
    struct vfs_node *d = vfs_lookup(path);
    if (!d || d->type != VFS_DIR)
        return PEAK_ENOTDIR;
    size_t o = 0;
    for (struct vfs_node *c = d->child; c; c = c->sibling) {
        size_t l = strlen(c->name);
        if (o + l + 2 >= out_len)
            break;
        memcpy(out + o, c->name, l);
        o += l;
        if (c->type == VFS_DIR)
            out[o++] = '/';
        else if (c->type == VFS_SYMLINK)
            out[o++] = '@';
        out[o++] = '\n';
    }
    out[o] = '\0';
    return 0;
}

/* Persistable PeakFS trees — restore replaces these namespaces.
 * Profile: 0=private (none), 1=workspace (/home), 2=full. */
int vfs_normalize(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len < 2)
        return PEAK_EINVAL;
    char parts[32][VFS_NAME_MAX];
    int depth = 0;
    size_t i = 0;
    if (path[0] != '/')
        return PEAK_EINVAL;
    i = 1;
    while (path[i] && depth < 32) {
        while (path[i] == '/')
            i++;
        if (!path[i])
            break;
        char part[VFS_NAME_MAX];
        size_t j = 0;
        while (path[i] && path[i] != '/') {
            if (j + 1 >= VFS_NAME_MAX)
                return PEAK_EINVAL;
            part[j++] = path[i++];
        }
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

static void node_path(struct vfs_node *n, char *out, size_t out_len) {
    char stack[16][VFS_NAME_MAX];
    int sp = 0;
    while (n && n != root && sp < 16) {
        memcpy(stack[sp], n->name, VFS_NAME_MAX);
        sp++;
        n = n->parent;
    }
    size_t o = 0;
    if (sp == 0) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }
    while (sp--) {
        if (o + 1 >= out_len)
            break;
        out[o++] = '/';
        for (size_t k = 0; stack[sp][k] && o + 1 < out_len; k++)
            out[o++] = stack[sp][k];
    }
    out[o] = '\0';
}

int vfs_stat(const char *path, struct vfs_stat *st) {
    if (!st)
        return PEAK_EINVAL;
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return PEAK_ENOENT;
    st->type = n->type;
    st->mode = n->mode;
    st->size = n->size;
    st->refs = n->refs;
    st->nchildren = 0;
    st->link_target[0] = '\0';
    for (struct vfs_node *c = n->child; c; c = c->sibling)
        st->nchildren++;
    memcpy(st->name, n->name, VFS_NAME_MAX);
    if (n->type == VFS_SYMLINK && n->data) {
        size_t tl = n->size; if (tl >= VFS_PATH_MAX) tl = VFS_PATH_MAX - 1;
        memcpy(st->link_target, n->data, tl); st->link_target[tl] = '\0';
    }
    return 0;
}

void vfs_mode_string(enum vfs_type type, uint16_t mode, char *buf, size_t len) {
    if (!buf || len < 11)
        return;
    buf[0] = (type == VFS_DIR) ? 'd' : '-';
    static const char *triples[] = { "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx" };
    buf[1] = triples[(mode >> 6) & 7][0];
    buf[2] = triples[(mode >> 6) & 7][1];
    buf[3] = triples[(mode >> 6) & 7][2];
    buf[4] = triples[(mode >> 3) & 7][0];
    buf[5] = triples[(mode >> 3) & 7][1];
    buf[6] = triples[(mode >> 3) & 7][2];
    buf[7] = triples[mode & 7][0];
    buf[8] = triples[mode & 7][1];
    buf[9] = triples[mode & 7][2];
    buf[10] = '\0';
}

int vfs_chmod(const char *path, uint16_t mode) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return PEAK_ENOENT;
    if (n == root)
        return PEAK_EINVAL;
    n->mode = mode & 0777u;
    vfs_note_workspace_dirty();
    return 0;
}

int vfs_exists(const char *path) { return vfs_lookup(path) != NULL; }
int vfs_is_dir(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    return n && n->type == VFS_DIR;
}
int vfs_is_file(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    return n && n->type == VFS_FILE;
}

int vfs_is_symlink(const char *path) { struct vfs_node *n = vfs_lookup(path); return n && n->type == VFS_SYMLINK; }
int vfs_readlink(const char *path, char *buf, size_t buf_len, size_t *out_len) {
    struct vfs_node *n = vfs_lookup(path); if (!n) return PEAK_ENOENT;
    if (n->type != VFS_SYMLINK) { vfs_set_error("not a symlink"); return PEAK_EINVAL; }
    size_t tlen = n->size; if (out_len) *out_len = tlen; if (!buf || buf_len == 0) return 0;
    size_t copy = tlen < buf_len - 1 ? tlen : buf_len - 1; memcpy(buf, n->data, copy); buf[copy] = '\0';
    vfs_set_error(""); return 0;
}
int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !target[0] || !linkpath) return PEAK_EINVAL;
    if (vfs_lookup(linkpath)) return PEAK_EEXIST;
    size_t tlen = strlen(target); if (tlen >= VFS_PATH_MAX) return PEAK_EINVAL;
    char parent[VFS_PATH_MAX]; size_t n = 0;
    while (linkpath[n] && n + 1 < VFS_PATH_MAX) { parent[n] = linkpath[n]; n++; }
    parent[n] = '\0'; int last = -1;
    for (size_t i = 0; parent[i]; i++) if (parent[i] == '/') last = (int)i;
    if (last < 0) return PEAK_EINVAL; parent[last] = '\0'; const char *leaf = linkpath + last + 1;
    if (last > 0 && !vfs_mkdir(parent)) return PEAK_ENOTDIR;
    struct vfs_node *dir = (last == 0) ? root : vfs_lookup(parent);
    if (!dir || dir->type != VFS_DIR) return PEAK_ENOTDIR;
    struct vfs_node *link = alloc_node(leaf, VFS_SYMLINK); if (!link) return PEAK_ENOMEM;
    uint8_t *buf = (uint8_t *)kmalloc(tlen + 1); if (!buf) return PEAK_ENOMEM;
    memcpy(buf, target, tlen); buf[tlen] = '\0'; link->data = buf; link->size = tlen; link->capacity = tlen + 1;
    add_child(dir, link); vfs_set_error(""); vfs_note_workspace_dirty(); return 0;
}

static void unlink_from_parent(struct vfs_node *n) {
    if (!n->parent)
        return;
    int pi = node_index(n->parent);
    int ci = node_index(n);
    unsigned b = name_bucket(n->name);
    struct vfs_node **pp = &n->parent->child;
    while (*pp) {
        if (*pp == n) {
            *pp = n->sibling;
            /* Keep bucket head honest so find_child skips the stale mid-chain path. */
            if (pi >= 0 && ci >= 0 && child_bucket[pi][b] == (uint16_t)(ci + 1)) {
                uint16_t next = 0;
                for (struct vfs_node *s = n->sibling; s; s = s->sibling) {
                    if (name_bucket(s->name) == b) {
                        int si = node_index(s);
                        if (si >= 0)
                            next = (uint16_t)(si + 1);
                        break;
                    }
                }
                child_bucket[pi][b] = next;
            }
            return;
        }
        pp = &(*pp)->sibling;
    }
}

int vfs_unlink(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return PEAK_ENOENT;
    if (n == root)
        return PEAK_EINVAL;
    if (n->type != VFS_FILE && n->type != VFS_SYMLINK)
        return PEAK_EISDIR;
    unlink_from_parent(n);
    int sharers = 0;
    if (n->type == VFS_FILE && n->data) {
        for (int i = 0; i < node_count; i++) {
            if (nodes[i].type == VFS_FILE && nodes[i].data == n->data)
                sharers++;
        }
    }
    if (sharers <= 1) {
        if (n->data)
            kfree(n->data);
    } else {
        /* decrement refs on remaining nodes with same data */
        for (int i = 0; i < node_count; i++) {
            if (nodes[i].type == VFS_FILE && nodes[i].data == n->data && &nodes[i] != n)
                if (nodes[i].refs > 1)
                    nodes[i].refs--;
        }
    }
    n->data = NULL;
    n->size = 0;
    n->blob_id = 0;
    n->type = 0;
    n->name[0] = '\0';
    n->parent = NULL;
    n->sibling = NULL;
    vfs_note_workspace_dirty();
    return 0;
}

int vfs_rmdir(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return PEAK_ENOENT;
    if (n == root)
        return PEAK_EINVAL;
    if (n->type != VFS_DIR)
        return PEAK_ENOTDIR;
    if (n->child)
        return PEAK_EBUSY;
    unlink_from_parent(n);
    n->type = 0;
    n->name[0] = '\0';
    n->parent = NULL;
    n->sibling = NULL;
    n->child = NULL;
    vfs_note_workspace_dirty();
    return 0;
}

int vfs_remove_tree(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n == root)
        return PEAK_EINVAL;
    if (n->type == VFS_FILE)
        return vfs_unlink(path);
    if (n->type == VFS_SYMLINK)
        return vfs_unlink(path);
    while (n->child) {
        char cp[VFS_PATH_MAX];
        node_path(n->child, cp, sizeof(cp));
        if (vfs_remove_tree(cp) != 0)
            return PEAK_EIO;
    }
    return vfs_rmdir(path);
}

int vfs_rename(const char *oldp, const char *newp) {
    struct vfs_node *n = vfs_lookup(oldp);
    if (!n || n == root)
        return PEAK_ENOENT;
    if (vfs_lookup(newp))
        return PEAK_EEXIST;
    char parent[VFS_PATH_MAX];
    size_t len = strlen(newp);
    if (len >= VFS_PATH_MAX)
        return PEAK_EINVAL;
    memcpy(parent, newp, len + 1);
    int last = -1;
    for (size_t i = 0; parent[i]; i++)
        if (parent[i] == '/')
            last = (int)i;
    if (last < 0)
        return PEAK_EINVAL;
    parent[last] = '\0';
    const char *leaf = newp + last + 1;
    struct vfs_node *dir = (last == 0) ? root : vfs_lookup(parent);
    if (!dir || dir->type != VFS_DIR)
        return PEAK_ENOTDIR;
    unlink_from_parent(n);
    size_t j = 0;
    for (; leaf[j] && j + 1 < VFS_NAME_MAX; j++)
        n->name[j] = leaf[j];
    n->name[j] = '\0';
    add_child(dir, n);
    vfs_note_workspace_dirty();
    return 0;
}

#define VFS_COPY_CHUNK (32 * 1024)

static int vfs_copy_stream(const char *src, const char *dst, size_t size,
                           int promote_blob, uint16_t mode) {
    if (promote_blob && blobstore_available()) {
        if (vfs_create_blob_file(dst, size) != 0)
            return PEAK_EIO;
    } else if (size == 0) {
        if (vfs_write_file(dst, (const uint8_t *)"", 0) != 0)
            return PEAK_EIO;
    }
    static uint8_t chunk[VFS_COPY_CHUNK];
    size_t off = 0;
    while (off < size) {
        size_t want = size - off;
        if (want > sizeof(chunk))
            want = sizeof(chunk);
        size_t n = 0;
        if (vfs_read_at(src, off, chunk, want, &n) != 0)
            return PEAK_EIO;
        if (n == 0)
            break;
        if (promote_blob && blobstore_available()) {
            if (vfs_write_at(dst, off, chunk, n) != 0)
                return PEAK_EIO;
        } else if (off == 0) {
            if (vfs_write_file(dst, chunk, n) != 0)
                return PEAK_EIO;
        } else if (vfs_write_at(dst, off, chunk, n) != 0) {
            return PEAK_EIO;
        }
        off += n;
    }
    struct vfs_node *d = vfs_lookup(dst);
    if (d)
        d->mode = mode;
    return 0;
}

int vfs_copy_file_ex(const char *src, const char *dst, int promote_blob) {
    struct vfs_node *s = vfs_lookup(src);
    if (!s || s->type != VFS_FILE)
        return PEAK_ENOENT;
    return vfs_copy_stream(src, dst, s->size, promote_blob, s->mode);
}

int vfs_copy_file(const char *src, const char *dst) {
    return vfs_copy_file_ex(src, dst, 0);
}

static int copy_tree_rec(struct vfs_node *src, const char *src_path,
                         const char *dst_path, int promote_blob) {
    if (src->type == VFS_FILE)
        return vfs_copy_stream(src_path, dst_path, src->size, promote_blob, src->mode);
    if (!vfs_mkdir(dst_path))
        return PEAK_EIO;
    struct vfs_node *d = vfs_lookup(dst_path);
    if (d)
        d->mode = src->mode;
    for (struct vfs_node *c = src->child; c; c = c->sibling) {
        char child_src[VFS_PATH_MAX];
        char child_dst[VFS_PATH_MAX];
        size_t o = 0;
        while (src_path[o] && o + 1 < VFS_PATH_MAX) {
            child_src[o] = src_path[o];
            o++;
        }
        if (o + 1 >= VFS_PATH_MAX)
            return PEAK_ENOSPC;
        child_src[o++] = '/';
        for (size_t k = 0; c->name[k] && o + 1 < VFS_PATH_MAX; k++)
            child_src[o++] = c->name[k];
        child_src[o] = '\0';
        o = 0;
        while (dst_path[o] && o + 1 < VFS_PATH_MAX) {
            child_dst[o] = dst_path[o];
            o++;
        }
        if (o + 1 >= VFS_PATH_MAX)
            return PEAK_ENOSPC;
        child_dst[o++] = '/';
        for (size_t k = 0; c->name[k] && o + 1 < VFS_PATH_MAX; k++)
            child_dst[o++] = c->name[k];
        child_dst[o] = '\0';
        if (copy_tree_rec(c, child_src, child_dst, promote_blob) != 0)
            return PEAK_EIO;
    }
    return 0;
}

int vfs_copy_tree_ex(const char *src, const char *dst, int promote_blob) {
    struct vfs_node *s = vfs_lookup(src);
    if (!s)
        return PEAK_ENOENT;
    return copy_tree_rec(s, src, dst, promote_blob);
}

int vfs_copy_tree(const char *src, const char *dst) {
    return vfs_copy_tree_ex(src, dst, 0);
}

int vfs_link(const char *target, const char *linkname) {
    struct vfs_node *t = vfs_lookup(target);
    if (!t || t->type != VFS_FILE)
        return PEAK_ENOENT;
    if (vfs_lookup(linkname))
        return PEAK_EEXIST;
    char parent[VFS_PATH_MAX];
    size_t len = strlen(linkname);
    memcpy(parent, linkname, len + 1);
    int last = -1;
    for (size_t i = 0; parent[i]; i++)
        if (parent[i] == '/')
            last = (int)i;
    if (last < 0)
        return PEAK_EINVAL;
    parent[last] = '\0';
    const char *leaf = linkname + last + 1;
    struct vfs_node *dir = (last == 0) ? root : vfs_lookup(parent);
    if (!dir || dir->type != VFS_DIR)
        return PEAK_ENOTDIR;
    struct vfs_node *link = alloc_node(leaf, VFS_FILE);
    if (!link)
        return PEAK_ENOMEM;
    link->data = t->data;
    link->size = t->size;
    link->capacity = t->capacity;
    link->mode = t->mode;
    t->refs++;
    link->refs = t->refs;
    add_child(dir, link);
    return 0;
}

static int walk_rec(struct vfs_node *n, char *path, size_t path_cap, size_t path_len,
                    vfs_walk_cb cb, void *ctx) {
    if (cb(path, n, ctx) != 0)
        return PEAK_EIO;
    if (n->type != VFS_DIR)
        return 0;
    for (struct vfs_node *c = n->child; c; c = c->sibling) {
        size_t o = path_len;
        if (o + 1 >= path_cap)
            return PEAK_ENOSPC;
        if (!(o == 1 && path[0] == '/')) {
            /* if path is "/" path_len is 1 */
        }
        if (path_len > 1 || (path_len == 1 && path[0] != '/')) {
            /* noop */
        }
        char child[VFS_PATH_MAX];
        size_t cl = 0;
        if (path_len == 1 && path[0] == '/') {
            child[cl++] = '/';
        } else {
            memcpy(child, path, path_len);
            cl = path_len;
            child[cl++] = '/';
        }
        for (size_t k = 0; c->name[k] && cl + 1 < sizeof(child); k++)
            child[cl++] = c->name[k];
        child[cl] = '\0';
        int wr = walk_rec(c, child, sizeof(child), cl, cb, ctx);
        if (wr != 0)
            return wr;
    }
    return 0;
}

int vfs_walk(const char *path, vfs_walk_cb cb, void *ctx) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return PEAK_ENOENT;
    char pbuf[VFS_PATH_MAX];
    size_t pl = 0;
    while (path[pl] && pl + 1 < sizeof(pbuf)) {
        pbuf[pl] = path[pl];
        pl++;
    }
    pbuf[pl] = '\0';
    return walk_rec(n, pbuf, sizeof(pbuf), pl, cb, ctx);
}

int vfs_readdir(const char *path, struct vfs_dirent *ents, int max_ents) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n->type != VFS_DIR || !ents || max_ents <= 0)
        return PEAK_EINVAL;
    int count = 0;
    for (struct vfs_node *c = n->child; c && count < max_ents; c = c->sibling) {
        memcpy(ents[count].name, c->name, VFS_NAME_MAX);
        ents[count].type = c->type;
        count++;
    }
    return count;
}

int vfs_node_count(void) {
    int used = 0;
    for (int i = 0; i < node_count; i++)
        if (nodes[i].name[0] || &nodes[i] == root)
            used++;
    return used;
}

static int bytes_cb(const char *path, struct vfs_node *node, void *ctx) {
    (void)path;
    uint64_t *sum = ctx;
    if (node->type == VFS_FILE)
        *sum += node->size;
    return 0;
}

uint64_t vfs_tree_bytes(const char *path) {
    uint64_t sum = 0;
    vfs_walk(path, bytes_cb, &sum);
    return sum;
}
