#include "vfs.h"
#include "vfs_path_util.h"
#include "peak_errno.h"
#include "util.h"
#include "privacy.h"
#include "heap.h"
#include "blobstore.h"

/* Keep in sync with AGENT_AUDIT_PATH in agent_internal.h */
#define PEAKFS_AUDIT_PATH "/var/peak/audit.log"
#define PEAKFS_AUDIT_PRESERVE_MAX (64u * 1024u)

static int peakfs_path_allowed(const char *path) {
    return peakfs_path_allowed_for_profile(path, privacy_persist_profile());
}

static void peakfs_clear_persist(void) {
    /*
     * PeakFS restore replaces persist namespaces. Preserve audit.log across the
     * clear so a workspace-only (or empty) restore cannot wipe the audit trail;
     * a full-profile blob that includes audit.log still overwrites after load.
     */
    char *audit_save = NULL;
    size_t audit_n = 0;
    {
        struct vfs_stat st;
        if (vfs_stat(PEAKFS_AUDIT_PATH, &st) == 0 && st.type == VFS_FILE && st.size > 0) {
            size_t cap = st.size;
            if (cap > PEAKFS_AUDIT_PRESERVE_MAX)
                cap = PEAKFS_AUDIT_PRESERVE_MAX;
            audit_save = (char *)kmalloc(cap);
            if (audit_save) {
                size_t got = 0;
                if (vfs_read_file(PEAKFS_AUDIT_PATH, audit_save, cap, &got) == 0 && got > 0)
                    audit_n = got;
                else {
                    kfree(audit_save);
                    audit_save = NULL;
                }
            }
        }
    }

    if (vfs_lookup("/home"))
        vfs_remove_tree("/home");
    if (vfs_lookup("/etc/peak"))
        vfs_remove_tree("/etc/peak");
    if (vfs_lookup("/var/peak"))
        vfs_remove_tree("/var/peak");
    vfs_mkdir("/home");
    vfs_mkdir("/etc/peak");
    vfs_mkdir("/var/peak");

    if (audit_save && audit_n) {
        vfs_write_file(PEAKFS_AUDIT_PATH, audit_save, audit_n);
        kfree(audit_save);
    } else if (audit_save) {
        kfree(audit_save);
    }
}

/* Peak ramdisk: magic "PEAKFS1\0", u32 count, then entries:
   u16 name_len, name bytes, u32 data_len, data bytes.
   Directories: path ends with '/' and data_len == 0. */
int vfs_load_ramdisk(const void *blob, size_t len) {
    const uint8_t *p = blob;
    if (len < 12 || memcmp(p, "PEAKFS1", 7) != 0)
        return PEAK_EIO;
    uint32_t count;
    memcpy(&count, p + 8, 4);
    size_t off = 12;
    /* Validate all entries before mutating. */
    for (uint32_t i = 0; i < count; i++) {
        if (off + 2 > len)
            return PEAK_EIO;
        uint16_t nlen;
        memcpy(&nlen, p + off, 2);
        off += 2;
        if (nlen == 0 || nlen >= VFS_PATH_MAX || off + nlen + 4 > len)
            return PEAK_EINVAL;
        char path[VFS_PATH_MAX];
        memcpy(path, p + off, nlen);
        path[nlen] = '\0';
        off += nlen;
        uint32_t dlen;
        memcpy(&dlen, p + off, 4);
        off += 4;
        if (off + dlen > len)
            return PEAK_EIO;
        int is_dir = (nlen > 0 && path[nlen - 1] == '/');
        if (is_dir) {
            if (dlen != 0)
                return PEAK_EINVAL;
            path[nlen - 1] = '\0';
        }
        if (!peakfs_path_allowed(path))
            return PEAK_EACCES;
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
                return PEAK_EIO;
        } else {
            if (vfs_write_file(path, p + off, dlen) != 0)
                return PEAK_EIO;
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
    /* Blob-backed large files: materialize through the LRU for PeakFS export.
     * Streaming export can replace this once the backend is fully wired. */
    if (node->blob_id) {
        if (node->size == 0)
            return export_write_entry(c, path, NULL, 0);
        uint8_t *tmp = (uint8_t *)kmalloc(node->size);
        if (!tmp) {
            c->err = 1;
            return -1;
        }
        if (blobstore_read(node->blob_id, 0, tmp, node->size) != (int)node->size) {
            kfree(tmp);
            c->err = 1;
            return -1;
        }
        int r = export_write_entry(c, path, tmp, node->size);
        kfree(tmp);
        return r;
    }
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

