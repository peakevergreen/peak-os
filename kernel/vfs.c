#include "vfs.h"
#include "heap.h"
#include "util.h"
#include "console.h"

static struct vfs_node nodes[VFS_MAX_NODES];
static int node_count;
static struct vfs_node *root;
/* First-character child buckets: nodes[i] -> heads[i][ch & 31]. */
static struct vfs_node *child_bucket[VFS_MAX_NODES][32];

static int node_index(struct vfs_node *n) {
    if (!n)
        return -1;
    intptr_t d = (intptr_t)(n - nodes);
    if (d < 0 || d >= VFS_MAX_NODES)
        return -1;
    return (int)d;
}

static unsigned name_bucket(const char *name) {
    return (unsigned)(unsigned char)name[0] & 31u;
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
    n->refs = 1;
    return n;
}

static void add_child(struct vfs_node *parent, struct vfs_node *child) {
    child->parent = parent;
    child->sibling = parent->child;
    parent->child = child;
    int pi = node_index(parent);
    if (pi >= 0) {
        unsigned b = name_bucket(child->name);
        /* Prepend keeps this child at the front of the sibling chain. */
        child_bucket[pi][b] = child;
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
    if (di >= 0 && child_bucket[di][b])
        start = child_bucket[di][b];
    for (struct vfs_node *c = start; c; c = c->sibling) {
        if (((unsigned)(unsigned char)c->name[0] & 31u) != b)
            continue;
        if (!strcmp(c->name, name))
            return c;
    }
    /* If we started mid-chain from a stale bucket head, scan from the real head. */
    if (start != dir->child) {
        for (struct vfs_node *c = dir->child; c && c != start; c = c->sibling) {
            if (((unsigned)(unsigned char)c->name[0] & 31u) != b)
                continue;
            if (!strcmp(c->name, name))
                return c;
        }
    }
    return NULL;
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
    if (existing)
        return existing;
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
        if (!n) {
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
    struct vfs_node *f = vfs_create_file(path);
    if (!f || f->type != VFS_FILE)
        return -1;
    uint8_t *buf = (uint8_t *)kmalloc(len ? len : 1);
    if (!buf)
        return -1;
    if (f->data)
        kfree(f->data);
    memcpy(buf, data, len);
    f->data = buf;
    f->size = len;
    f->capacity = len;
    return 0;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    struct vfs_node *f = vfs_lookup(path);
    if (!f || f->type != VFS_FILE)
        return -1;
    size_t n = f->size < buf_len ? f->size : buf_len;
    memcpy(buf, f->data, n);
    if (out_len)
        *out_len = n;
    return 0;
}

int vfs_list(const char *path, char *out, size_t out_len) {
    struct vfs_node *d = vfs_lookup(path);
    if (!d || d->type != VFS_DIR)
        return -1;
    size_t o = 0;
    for (struct vfs_node *c = d->child; c; c = c->sibling) {
        size_t l = strlen(c->name);
        if (o + l + 2 >= out_len)
            break;
        memcpy(out + o, c->name, l);
        o += l;
        if (c->type == VFS_DIR)
            out[o++] = '/';
        out[o++] = '\n';
    }
    out[o] = '\0';
    return 0;
}

/* Persistable PeakFS trees — restore replaces these namespaces.
 * Profile: 0=private (none), 1=workspace (/home), 2=full. */
static int peakfs_path_allowed(const char *path) {
    if (!path || path[0] != '/')
        return 0;
    /* Reject ".." components and empty segments. */
    for (size_t i = 0; path[i]; i++) {
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == '/' || path[i + 2] == '\0'))
            return 0;
    }
    extern int privacy_persist_profile(void);
    int profile = privacy_persist_profile();
    if (profile <= 0)
        return 0; /* private / ephemeral */
    if (profile == 1) {
        size_t pl = 5; /* "/home" */
        return strncmp(path, "/home", pl) == 0 &&
               (path[pl] == '\0' || path[pl] == '/');
    }
    static const char *const prefixes[] = {
        "/home", "/etc/peak", "/var/peak", NULL
    };
    for (int i = 0; prefixes[i]; i++) {
        size_t pl = strlen(prefixes[i]);
        if (strncmp(path, prefixes[i], pl) == 0 &&
            (path[pl] == '\0' || path[pl] == '/'))
            return 1;
    }
    return 0;
}

static void peakfs_clear_persist(void) {
    if (vfs_lookup("/home"))
        vfs_remove_tree("/home");
    if (vfs_lookup("/etc/peak"))
        vfs_remove_tree("/etc/peak");
    if (vfs_lookup("/var/peak"))
        vfs_remove_tree("/var/peak");
    vfs_mkdir("/home");
    vfs_mkdir("/etc/peak");
    vfs_mkdir("/var/peak");
}

/* Peak ramdisk: magic "PEAKFS1\0", u32 count, then entries:
   u16 name_len, name bytes, u32 data_len, data bytes.
   Directories: path ends with '/' and data_len == 0. */
int vfs_load_ramdisk(const void *blob, size_t len) {
    const uint8_t *p = blob;
    if (len < 12 || memcmp(p, "PEAKFS1", 7) != 0)
        return -1;
    uint32_t count;
    memcpy(&count, p + 8, 4);
    size_t off = 12;
    /* Validate all entries before mutating. */
    for (uint32_t i = 0; i < count; i++) {
        if (off + 2 > len)
            return -1;
        uint16_t nlen;
        memcpy(&nlen, p + off, 2);
        off += 2;
        if (nlen == 0 || nlen >= VFS_PATH_MAX || off + nlen + 4 > len)
            return -1;
        char path[VFS_PATH_MAX];
        memcpy(path, p + off, nlen);
        path[nlen] = '\0';
        off += nlen;
        uint32_t dlen;
        memcpy(&dlen, p + off, 4);
        off += 4;
        if (off + dlen > len)
            return -1;
        int is_dir = (nlen > 0 && path[nlen - 1] == '/');
        if (is_dir) {
            if (dlen != 0)
                return -1;
            path[nlen - 1] = '\0';
        }
        if (!peakfs_path_allowed(path))
            return -1;
        off += dlen;
    }
    /* Replace persisted namespaces rather than overlaying. */
    peakfs_clear_persist();
    off = 12;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t nlen;
        memcpy(&nlen, p + off, 2);
        off += 2;
        char path[VFS_PATH_MAX];
        memcpy(path, p + off, nlen);
        path[nlen] = '\0';
        off += nlen;
        uint32_t dlen;
        memcpy(&dlen, p + off, 4);
        off += 4;
        if (nlen > 0 && path[nlen - 1] == '/') {
            path[nlen - 1] = '\0';
            if (!vfs_mkdir(path))
                return -1;
        } else {
            if (vfs_write_file(path, p + off, dlen) != 0)
                return -1;
        }
        off += dlen;
    }
    return 0;
}

struct export_ctx {
    uint8_t *blob;
    size_t cap;
    size_t off;
    uint32_t count;
    int err;
};

static int export_write_entry(struct export_ctx *c, const char *path,
                              const void *data, size_t dlen) {
    size_t nlen = strlen(path);
    if (nlen == 0 || nlen >= 65535)
        return 0;
    size_t need = 2 + nlen + 4 + dlen;
    if (c->off + need > c->cap) {
        c->err = 1;
        return 1;
    }
    uint16_t nl = (uint16_t)nlen;
    memcpy(c->blob + c->off, &nl, 2);
    c->off += 2;
    memcpy(c->blob + c->off, path, nlen);
    c->off += nlen;
    uint32_t dl = (uint32_t)dlen;
    memcpy(c->blob + c->off, &dl, 4);
    c->off += 4;
    if (dlen && data)
        memcpy(c->blob + c->off, data, dlen);
    c->off += dlen;
    c->count++;
    return 0;
}

static int export_cb(const char *path, struct vfs_node *node, void *vctx) {
    struct export_ctx *c = vctx;
    if (!node || !path || !peakfs_path_allowed(path))
        return 0;
    if (node->type == VFS_DIR) {
        char dpath[VFS_PATH_MAX];
        size_t pl = strlen(path);
        if (pl + 2 >= sizeof(dpath))
            return 0;
        memcpy(dpath, path, pl);
        dpath[pl] = '/';
        dpath[pl + 1] = '\0';
        return export_write_entry(c, dpath, NULL, 0);
    }
    if (node->type != VFS_FILE)
        return 0;
    return export_write_entry(c, path, node->data, node->size);
}

struct size_ctx {
    size_t bytes;
    uint32_t count;
};

static int size_cb(const char *path, struct vfs_node *node, void *vctx) {
    struct size_ctx *c = vctx;
    if (!node || !path || !peakfs_path_allowed(path))
        return 0;
    size_t nlen = strlen(path);
    if (node->type == VFS_DIR)
        nlen += 1; /* trailing '/' */
    if (nlen == 0 || nlen >= 65535)
        return 0;
    size_t dlen = (node->type == VFS_FILE) ? node->size : 0;
    c->bytes += 2 + nlen + 4 + dlen;
    c->count++;
    return 0;
}

int vfs_export_ramdisk_size(void) {
    struct size_ctx c = { .bytes = 12, .count = 0 };
    vfs_walk("/home", size_cb, &c);
    vfs_walk("/etc/peak", size_cb, &c);
    vfs_walk("/var/peak", size_cb, &c);
    if (c.bytes > (size_t)0x7fffffff)
        return -1;
    return (int)c.bytes;
}

int vfs_export_ramdisk(void *blob, size_t cap) {
    if (!blob || cap < 12)
        return -1;
    uint8_t *p = blob;
    memcpy(p, "PEAKFS1", 7);
    p[7] = 0;
    struct export_ctx c = { .blob = p, .cap = cap, .off = 12, .count = 0, .err = 0 };
    vfs_walk("/home", export_cb, &c);
    vfs_walk("/etc/peak", export_cb, &c);
    vfs_walk("/var/peak", export_cb, &c);
    if (c.err)
        return -1;
    memcpy(p + 8, &c.count, 4);
    return (int)c.off;
}

void vfs_seed_defaults(void) {
    vfs_mkdir("/bin");
    vfs_mkdir("/home");
    vfs_mkdir("/home/dev");
    vfs_mkdir("/home/dev/workspace");
    vfs_mkdir("/var");
    vfs_mkdir("/var/peak");
    vfs_mkdir("/var/peak/sessions");
    vfs_mkdir("/var/peak/vec");
    vfs_mkdir("/var/lib");
    vfs_mkdir("/var/lib/peak-ctr");
    vfs_mkdir("/var/lib/peak-ctr/images");
    vfs_mkdir("/etc");
    vfs_mkdir("/etc/peak");

    const char *readme =
        "# Peak workspace\n\n"
        "AI-first developer workspace. Try: ask \"create hello.c\"\n";
    vfs_write_file("/home/dev/workspace/README.md", readme, strlen(readme));

    const char *policy =
        "allow_paths=/home/dev/workspace,/var/peak/sessions\n"
        "allow_tools=fs.read,fs.write,fs.list,console.print\n"
        "deny_tools=proc.exec\n"
        "require_approval=fs.write\n";
    vfs_write_file("/etc/peak/agent.policy", policy, strlen(policy));

    const char *hello =
        "/* hello.c — sample workspace file */\nint main(void) { return 0; }\n";
    vfs_write_file("/home/dev/workspace/hello.c", hello, strlen(hello));

    /* Demo container app — build with: ctr build && ctr run */
    vfs_mkdir("/home/dev/workspace/demo");
    const char *dockerfile =
        "FROM nginx:alpine\n"
        "COPY index.html /usr/share/nginx/html/index.html\n";
    vfs_write_file("/home/dev/workspace/demo/Dockerfile", dockerfile, strlen(dockerfile));

    const char *index_html =
        "<!DOCTYPE html>\n"
        "<html><head><meta charset=\"utf-8\">\n"
        "<title>Peak Evergreen</title>\n"
        "<style>\n"
        "body{background:#0B1A12;color:#E8F0EA}\n"
        "h1{color:#3DA36A}\n"
        "h2{color:#3DA36A}\n"
        "a{color:#9AC4AE}\n"
        "code{color:#3DA36A}\n"
        "</style></head><body>\n"
        "<h1>Peak Evergreen</h1>\n"
        "<p>Your Dockerfile is live inside Peak OS &mdash; built, served, and\n"
        "viewed entirely in-guest. No host Docker required.</p>\n"
        "<h2>What just happened</h2>\n"
        "<ul>\n"
        "<li>ctr build staged this page into an image rootfs</li>\n"
        "<li>ctr run exposed it on virtual port 18080</li>\n"
        "<li>Peak Browser rendered the HTML with real layout</li>\n"
        "</ul>\n"
        "<h2>Try next</h2>\n"
        "<p>Edit <code>index.html</code> in the demo folder, rebuild, and reload.\n"
        "Links work too: <a href=\"/\">refresh this page</a>.</p>\n"
        "<hr>\n"
        "<blockquote>Built on Peak. Served on Peak. Viewed on Peak.</blockquote>\n"
        "</body></html>\n";
    vfs_write_file("/home/dev/workspace/demo/index.html", index_html, strlen(index_html));

    const char *demo_readme =
        "# Peak demo container\n\n"
        "All in-guest (no host Docker):\n\n"
        "  ctr build\n"
        "  ctr run\n"
        "  gui  → Browser → http://127.0.0.1:18080/\n"
        "  LAN: curl http://<guest-ip>:18080/\n";
    vfs_write_file("/home/dev/workspace/demo/README.md", demo_readme, strlen(demo_readme));

    /* LAN webpage demo — separate port for multi-container tests */
    vfs_mkdir("/home/dev/workspace/web-demo");
    const char *web_df =
        "FROM nginx:alpine\n"
        "COPY index.html /usr/share/nginx/html/index.html\n"
        "COPY style.css /usr/share/nginx/html/style.css\n"
        "EXPOSE 8080\n";
    vfs_write_file("/home/dev/workspace/web-demo/Dockerfile", web_df, strlen(web_df));

    const char *web_html =
        "<!DOCTYPE html>\n"
        "<html lang=\"en\"><head><meta charset=\"utf-8\">\n"
        "<title>Peak Web Demo</title>\n"
        "<link rel=\"stylesheet\" href=\"/style.css\">\n"
        "</head><body>\n"
        "<main>\n"
        "<h1>Peak OS</h1>\n"
        "<p class=\"tag\">LAN static site demo</p>\n"
        "<p>This page is served by Peak&rsquo;s in-guest container runtime over\n"
        "real TCP. Open it from another device on your network using the\n"
        "guest IP from <code>ifconfig</code>.</p>\n"
        "<ul>\n"
        "<li><code>ctr build /home/dev/workspace/web-demo -t peak/web:latest</code></li>\n"
        "<li><code>ctr run -p 8080 --name peak-web peak/web:latest</code></li>\n"
        "<li><code>curl http://&lt;guest-ip&gt;:8080/</code></li>\n"
        "</ul>\n"
        "</main>\n"
        "</body></html>\n";
    vfs_write_file("/home/dev/workspace/web-demo/index.html", web_html, strlen(web_html));

    const char *web_css =
        "body{margin:0;font-family:Georgia,serif;background:#10241A;color:#E8F0EA}\n"
        "main{max-width:40rem;margin:4rem auto;padding:0 1.5rem}\n"
        "h1{font-size:2.4rem;letter-spacing:0.04em;color:#3DA36A;margin:0 0 0.25rem}\n"
        ".tag{color:#9AC4AE;text-transform:uppercase;letter-spacing:0.12em;font-size:0.85rem}\n"
        "code{color:#3DA36A}\n"
        "a{color:#9AC4AE}\n";
    vfs_write_file("/home/dev/workspace/web-demo/style.css", web_css, strlen(web_css));

    const char *web_readme =
        "# Peak web-demo (LAN)\n\n"
        "  ctr build /home/dev/workspace/web-demo -t peak/web:latest\n"
        "  ctr run -p 8080 --name peak-web peak/web:latest\n"
        "  ifconfig   # note inet address\n"
        "  # from another LAN computer:\n"
        "  #   curl http://<guest-ip>:8080/\n";
    vfs_write_file("/home/dev/workspace/web-demo/README.md", web_readme, strlen(web_readme));
}

int vfs_normalize(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len < 2)
        return -1;
    char parts[32][VFS_NAME_MAX];
    int depth = 0;
    size_t i = 0;
    if (path[0] != '/')
        return -1;
    i = 1;
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
            return -1;
        out[o++] = '/';
        for (size_t k = 0; parts[p][k]; k++) {
            if (o + 1 >= out_len)
                return -1;
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
    struct vfs_node *n = vfs_lookup(path);
    if (!n || !st)
        return -1;
    st->type = n->type;
    st->size = n->size;
    st->refs = n->refs;
    st->nchildren = 0;
    for (struct vfs_node *c = n->child; c; c = c->sibling)
        st->nchildren++;
    memcpy(st->name, n->name, VFS_NAME_MAX);
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

static void unlink_from_parent(struct vfs_node *n) {
    if (!n->parent)
        return;
    struct vfs_node **pp = &n->parent->child;
    while (*pp) {
        if (*pp == n) {
            *pp = n->sibling;
            return;
        }
        pp = &(*pp)->sibling;
    }
}

int vfs_unlink(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n->type != VFS_FILE || n == root)
        return -1;
    unlink_from_parent(n);
    /* Count sharers of this data pointer */
    int sharers = 0;
    if (n->data) {
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
    n->type = 0;
    n->name[0] = '\0';
    n->parent = NULL;
    n->sibling = NULL;
    return 0;
}

int vfs_rmdir(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n->type != VFS_DIR || n == root)
        return -1;
    if (n->child)
        return -1;
    unlink_from_parent(n);
    n->type = 0;
    n->name[0] = '\0';
    n->parent = NULL;
    n->sibling = NULL;
    n->child = NULL;
    return 0;
}

int vfs_remove_tree(const char *path) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n == root)
        return -1;
    if (n->type == VFS_FILE)
        return vfs_unlink(path);
    while (n->child) {
        char cp[VFS_PATH_MAX];
        node_path(n->child, cp, sizeof(cp));
        if (vfs_remove_tree(cp) != 0)
            return -1;
    }
    return vfs_rmdir(path);
}

int vfs_rename(const char *oldp, const char *newp) {
    struct vfs_node *n = vfs_lookup(oldp);
    if (!n || n == root)
        return -1;
    if (vfs_lookup(newp))
        return -1;
    char parent[VFS_PATH_MAX];
    size_t len = strlen(newp);
    if (len >= VFS_PATH_MAX)
        return -1;
    memcpy(parent, newp, len + 1);
    int last = -1;
    for (size_t i = 0; parent[i]; i++)
        if (parent[i] == '/')
            last = (int)i;
    if (last < 0)
        return -1;
    parent[last] = '\0';
    const char *leaf = newp + last + 1;
    struct vfs_node *dir = (last == 0) ? root : vfs_lookup(parent);
    if (!dir || dir->type != VFS_DIR)
        return -1;
    unlink_from_parent(n);
    size_t j = 0;
    for (; leaf[j] && j + 1 < VFS_NAME_MAX; j++)
        n->name[j] = leaf[j];
    n->name[j] = '\0';
    add_child(dir, n);
    return 0;
}

int vfs_copy_file(const char *src, const char *dst) {
    struct vfs_node *s = vfs_lookup(src);
    if (!s || s->type != VFS_FILE)
        return -1;
    return vfs_write_file(dst, s->data ? s->data : (const uint8_t *)"", s->size);
}

static int copy_tree_rec(struct vfs_node *src, const char *dst_path) {
    if (src->type == VFS_FILE)
        return vfs_write_file(dst_path, src->data ? src->data : (const uint8_t *)"", src->size);
    if (!vfs_mkdir(dst_path))
        return -1;
    for (struct vfs_node *c = src->child; c; c = c->sibling) {
        char child[VFS_PATH_MAX];
        size_t o = 0;
        while (dst_path[o] && o + 1 < VFS_PATH_MAX) {
            child[o] = dst_path[o];
            o++;
        }
        if (o + 1 >= VFS_PATH_MAX)
            return -1;
        child[o++] = '/';
        for (size_t k = 0; c->name[k] && o + 1 < VFS_PATH_MAX; k++)
            child[o++] = c->name[k];
        child[o] = '\0';
        if (copy_tree_rec(c, child) != 0)
            return -1;
    }
    return 0;
}

int vfs_copy_tree(const char *src, const char *dst) {
    struct vfs_node *s = vfs_lookup(src);
    if (!s)
        return -1;
    return copy_tree_rec(s, dst);
}

int vfs_link(const char *target, const char *linkname) {
    struct vfs_node *t = vfs_lookup(target);
    if (!t || t->type != VFS_FILE)
        return -1;
    if (vfs_lookup(linkname))
        return -1;
    char parent[VFS_PATH_MAX];
    size_t len = strlen(linkname);
    memcpy(parent, linkname, len + 1);
    int last = -1;
    for (size_t i = 0; parent[i]; i++)
        if (parent[i] == '/')
            last = (int)i;
    if (last < 0)
        return -1;
    parent[last] = '\0';
    const char *leaf = linkname + last + 1;
    struct vfs_node *dir = (last == 0) ? root : vfs_lookup(parent);
    if (!dir || dir->type != VFS_DIR)
        return -1;
    struct vfs_node *link = alloc_node(leaf, VFS_FILE);
    if (!link)
        return -1;
    link->data = t->data;
    link->size = t->size;
    link->capacity = t->capacity;
    t->refs++;
    link->refs = t->refs;
    add_child(dir, link);
    return 0;
}

static int walk_rec(struct vfs_node *n, char *path, size_t path_cap, size_t path_len,
                    vfs_walk_cb cb, void *ctx) {
    if (cb(path, n, ctx) != 0)
        return -1;
    if (n->type != VFS_DIR)
        return 0;
    for (struct vfs_node *c = n->child; c; c = c->sibling) {
        size_t o = path_len;
        if (o + 1 >= path_cap)
            return -1;
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
        if (walk_rec(c, child, sizeof(child), cl, cb, ctx) != 0)
            return -1;
    }
    return 0;
}

int vfs_walk(const char *path, vfs_walk_cb cb, void *ctx) {
    struct vfs_node *n = vfs_lookup(path);
    if (!n)
        return -1;
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
        return -1;
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
