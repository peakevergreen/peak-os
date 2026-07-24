#include "ctr.h"
#include "ctr_internal.h"
#include "vfs.h"
#include "util.h"

static void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t')
        (*p)++;
}

static int parse_word(const char **p, char *out, size_t out_cap) {
    skip_ws(p);
    size_t o = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r' &&
           o + 1 < out_cap) {
        out[o++] = *(*p)++;
    }
    out[o] = '\0';
    return o > 0 ? 0 : -1;
}

static int ensure_dir_for_file(const char *path) {
    char parent[CTR_PATH_MAX];
    size_t n = 0;
    while (path[n] && n + 1 < sizeof(parent)) {
        parent[n] = path[n];
        n++;
    }
    parent[n] = '\0';
    int last = -1;
    for (size_t i = 0; parent[i]; i++)
        if (parent[i] == '/')
            last = (int)i;
    if (last <= 0)
        return 0;
    parent[last] = '\0';
    return vfs_mkdir(parent) ? 0 : -1;
}

static int copy_into_rootfs(const char *context, const char *src_rel,
                           const char *dest_abs, const char *rootfs,
                           char *log, size_t log_cap) {
    char src[CTR_PATH_MAX];
    char dst[CTR_PATH_MAX];
    ctr_join_path(context, src_rel, src, sizeof(src));
    if (!ctr_path_under_rootfs(context, src)) {
        ctr_log_append(log, log_cap, "COPY src escapes context");
        return -1;
    }

    if (dest_abs[0] == '/')
        snprintf(dst, sizeof(dst), "%s%s", rootfs, dest_abs);
    else
        snprintf(dst, sizeof(dst), "%s/%s", rootfs, dest_abs);

    size_t dl = strlen(dst);
    if (dl > 0 && dst[dl - 1] == '/') {
        const char *base = src_rel;
        for (const char *q = src_rel; *q; q++)
            if (*q == '/')
                base = q + 1;
        snprintf(dst + dl, sizeof(dst) - dl, "%s", base);
    }

    if (!ctr_path_under_rootfs(rootfs, dst)) {
        ctr_log_append(log, log_cap, "COPY dest escapes rootfs");
        return -1;
    }

    if (!vfs_is_file(src)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "COPY miss: %s", src);
        ctr_log_append(log, log_cap, msg);
        return -1;
    }
    if (ensure_dir_for_file(dst) != 0)
        return -1;
    if (vfs_copy_file(src, dst) != 0) {
        ctr_log_append(log, log_cap, "COPY write failed");
        return -1;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "COPY %s -> %s", src_rel, dest_abs);
    ctr_log_append(log, log_cap, msg);
    return 0;
}

int ctr_build(const char *context_dir, const char *tag, char *log, size_t log_cap) {
    ctr_init();
    if (log && log_cap)
        log[0] = '\0';
    if (!context_dir || !tag || !tag[0])
        return -1;

    char dfpath[CTR_PATH_MAX];
    snprintf(dfpath, sizeof(dfpath), "%s/Dockerfile", context_dir);
    if (!vfs_is_file(dfpath)) {
        char msg[192];
        snprintf(msg, sizeof(msg), "no Dockerfile in %s", context_dir);
        ctr_log_append(log, log_cap, msg);
        return -1;
    }

    char rootfs[CTR_PATH_MAX];
    ctr_image_rootfs_path(tag, rootfs, sizeof(rootfs));

    if (vfs_exists(rootfs))
        vfs_remove_tree(rootfs);
    if (!vfs_mkdir(rootfs)) {
        ctr_log_append(log, log_cap, "cannot create rootfs");
        return -1;
    }

    char df[4096];
    size_t n = 0;
    if (vfs_read_file(dfpath, df, sizeof(df) - 1, &n) != 0) {
        ctr_log_append(log, log_cap, "cannot read Dockerfile");
        return -1;
    }
    df[n] = '\0';

    char msg[128];
    snprintf(msg, sizeof(msg), "building %s from %s", tag, context_dir);
    ctr_log_append(log, log_cap, msg);

    int copies = 0;
    int from_quarantined = 0;
    char expose_port[16] = "";
    const char *p = df;
    while (*p) {
        char line[256];
        size_t li = 0;
        while (*p && *p != '\n' && li + 1 < sizeof(line))
            line[li++] = *p++;
        if (*p == '\n')
            p++;
        line[li] = '\0';
        if (li > 0 && line[li - 1] == '\r')
            line[--li] = '\0';

        const char *lp = line;
        skip_ws(&lp);
        if (!*lp || *lp == '#')
            continue;

        if (!strncmp(lp, "FROM", 4) && (lp[4] == ' ' || lp[4] == '\t')) {
            lp += 4;
            char base[64];
            if (parse_word(&lp, base, sizeof(base)) == 0) {
                from_quarantined = 1;
                snprintf(msg, sizeof(msg),
                         "QUARANTINE FROM %s: no registry pull (COPY-only build)", base);
                ctr_log_append(log, log_cap, msg);
                if (ctr_str_has(base, "nginx")) {
                    char html[CTR_PATH_MAX];
                    snprintf(html, sizeof(html), "%s/usr/share/nginx/html", rootfs);
                    vfs_mkdir(html);
                    ctr_log_append(log, log_cap,
                                   "note: mkdir html path only (base image not fetched)");
                }
            }
            continue;
        }

        if (!strncmp(lp, "COPY", 4) && (lp[4] == ' ' || lp[4] == '\t')) {
            lp += 4;
            char src[128], dest[128];
            if (parse_word(&lp, src, sizeof(src)) != 0 ||
                parse_word(&lp, dest, sizeof(dest)) != 0) {
                ctr_log_append(log, log_cap, "COPY needs src dest");
                return -1;
            }
            if (copy_into_rootfs(context_dir, src, dest, rootfs, log, log_cap) != 0)
                return -1;
            copies++;
            continue;
        }

        if (!strncmp(lp, "EXPOSE", 6) && (lp[6] == ' ' || lp[6] == '\t')) {
            lp += 6;
            char pw[16];
            if (parse_word(&lp, pw, sizeof(pw)) == 0 && ctr_parse_port(pw)) {
                snprintf(expose_port, sizeof(expose_port), "%s", pw);
                snprintf(msg, sizeof(msg), "EXPOSE %s", pw);
            } else {
                snprintf(msg, sizeof(msg), "skip: %s", line);
            }
            ctr_log_append(log, log_cap, msg);
            continue;
        }

        if (!strncmp(lp, "WORKDIR", 7) || !strncmp(lp, "CMD", 3) ||
            !strncmp(lp, "ENV", 3) ||
            !strncmp(lp, "RUN", 3) || !strncmp(lp, "ENTRYPOINT", 10)) {
            snprintf(msg, sizeof(msg), "skip: %s", line);
            ctr_log_append(log, log_cap, msg);
            continue;
        }

        snprintf(msg, sizeof(msg), "unknown: %s", line);
        ctr_log_append(log, log_cap, msg);
    }

    char meta[CTR_PATH_MAX];
    ctr_image_meta_path(tag, meta, sizeof(meta));
    char metabuf[128];
    snprintf(metabuf, sizeof(metabuf), "%s\nexpose=%s\n", tag, expose_port);
    vfs_write_file(meta, metabuf, strlen(metabuf));

    snprintf(last_image, sizeof(last_image), "%s", tag);
    snprintf(last_port, sizeof(last_port), "%s", expose_port);

    snprintf(msg, sizeof(msg), "ok - %d COPY step(s), image %s", copies, tag);
    ctr_log_append(log, log_cap, msg);
    if (from_quarantined)
        ctr_log_append(log, log_cap,
                       "warning: FROM is quarantined — no image was pulled from a registry");
    return 0;
}

int ctr_last_built(char *tag, size_t tag_cap, char *port, size_t port_cap) {
    if (!last_image[0])
        return -1;
    if (tag && tag_cap)
        snprintf(tag, tag_cap, "%s", last_image);
    if (port && port_cap)
        snprintf(port, port_cap, "%s", last_port);
    return 0;
}

int ctr_image_expose(const char *tag, char *port, size_t port_cap) {
    if (!tag || !port || !port_cap)
        return -1;
    port[0] = '\0';
    char meta[CTR_PATH_MAX];
    ctr_image_meta_path(tag, meta, sizeof(meta));
    char buf[128];
    size_t n = 0;
    if (vfs_read_file(meta, buf, sizeof(buf) - 1, &n) != 0 || n == 0)
        return -1;
    buf[n] = '\0';
    const char *e = buf;
    while (*e) {
        if (!strncmp(e, "expose=", 7)) {
            e += 7;
            size_t o = 0;
            while (*e && *e != '\n' && o + 1 < port_cap)
                port[o++] = *e++;
            port[o] = '\0';
            return port[0] ? 0 : -1;
        }
        while (*e && *e != '\n')
            e++;
        if (*e == '\n')
            e++;
    }
    return -1;
}
