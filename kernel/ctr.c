#include "ctr.h"
#include "http_util.h"
#include "net.h"
#include "vfs.h"
#include "util.h"

#define CTR_MAX_CONTAINERS 8
#define CTR_TAG_MAX        64
#define CTR_NAME_MAX       48
#define CTR_PATH_MAX       VFS_PATH_MAX
#define CTR_LOG_MAX        1024
#define CTR_IMG_ROOT       "/var/lib/peak-ctr/images"
#define CTR_HTTP_BODY_MAX  24576
#define CTR_HTTP_REQ_MAX   2048

struct ctr_container {
    int used;
    int running;
    int listen_id;
    uint16_t port_num;
    char name[CTR_NAME_MAX];
    char image[CTR_TAG_MAX];
    char port[16];
    char rootfs[CTR_PATH_MAX];
    char log[CTR_LOG_MAX];
};

static struct ctr_container containers[CTR_MAX_CONTAINERS];
static int ctr_ready;

/* Most recently built image this boot, so `ctr run` needs no arguments. */
static char last_image[CTR_TAG_MAX];
static char last_port[16];

static int str_has(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (!nlen)
        return 1;
    for (const char *p = hay; *p; p++) {
        if (!strncmp(p, needle, nlen))
            return 1;
    }
    return 0;
}

static int contains(const char *hay, const char *needle) {
    return str_has(hay, needle);
}

static void log_append(char *log, size_t cap, const char *line) {
    if (!log || !cap)
        return;
    size_t n = strlen(log);
    size_t ln = strlen(line);
    if (n + ln + 2 >= cap)
        return;
    memcpy(log + n, line, ln);
    n += ln;
    log[n++] = '\n';
    log[n] = '\0';
}

static void sanitize_tag(const char *tag, char *out, size_t out_cap) {
    size_t o = 0;
    for (size_t i = 0; tag[i] && o + 1 < out_cap; i++) {
        char c = tag[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[o++] = c;
        else
            out[o++] = '_';
    }
    if (o == 0 && out_cap > 1) {
        out[0] = 'i';
        o = 1;
    }
    out[o] = '\0';
}

static void image_rootfs_path(const char *tag, char *out, size_t out_cap) {
    char safe[CTR_TAG_MAX];
    sanitize_tag(tag, safe, sizeof(safe));
    snprintf(out, out_cap, "%s/%s/rootfs", CTR_IMG_ROOT, safe);
}

static void image_meta_path(const char *tag, char *out, size_t out_cap) {
    char safe[CTR_TAG_MAX];
    sanitize_tag(tag, safe, sizeof(safe));
    snprintf(out, out_cap, "%s/%s/tag", CTR_IMG_ROOT, safe);
}

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

static void join_path(const char *base, const char *rel, char *out, size_t out_cap) {
    if (rel[0] == '/') {
        snprintf(out, out_cap, "%s", rel);
        return;
    }
    size_t bl = strlen(base);
    if (bl > 0 && base[bl - 1] == '/')
        snprintf(out, out_cap, "%s%s", base, rel);
    else
        snprintf(out, out_cap, "%s/%s", base, rel);
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
    join_path(context, src_rel, src, sizeof(src));

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

    if (!vfs_is_file(src)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "COPY miss: %s", src);
        log_append(log, log_cap, msg);
        return -1;
    }
    if (ensure_dir_for_file(dst) != 0)
        return -1;
    if (vfs_copy_file(src, dst) != 0) {
        log_append(log, log_cap, "COPY write failed");
        return -1;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "COPY %s -> %s", src_rel, dest_abs);
    log_append(log, log_cap, msg);
    return 0;
}

static struct ctr_container *find_by_name(const char *name) {
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        if (containers[i].used && !strcmp(containers[i].name, name))
            return &containers[i];
    }
    return NULL;
}

static struct ctr_container *find_by_port(const char *port) {
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        if (containers[i].used && containers[i].running &&
            !strcmp(containers[i].port, port))
            return &containers[i];
    }
    return NULL;
}

static struct ctr_container *alloc_slot(void) {
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        if (!containers[i].used)
            return &containers[i];
    }
    return NULL;
}

static uint16_t parse_port(const char *port) {
    uint32_t v = 0;
    if (!port || !port[0])
        return 0;
    for (const char *p = port; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10u + (uint32_t)(*p - '0');
        if (v > 65535)
            return 0;
    }
    return (uint16_t)v;
}

void ctr_init(void) {
    if (ctr_ready)
        return;
    vfs_mkdir("/var/lib");
    vfs_mkdir("/var/lib/peak-ctr");
    vfs_mkdir(CTR_IMG_ROOT);
    memset(containers, 0, sizeof(containers));
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++)
        containers[i].listen_id = -1;
    ctr_ready = 1;
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
        log_append(log, log_cap, msg);
        return -1;
    }

    char rootfs[CTR_PATH_MAX];
    image_rootfs_path(tag, rootfs, sizeof(rootfs));

    if (vfs_exists(rootfs))
        vfs_remove_tree(rootfs);
    if (!vfs_mkdir(rootfs)) {
        log_append(log, log_cap, "cannot create rootfs");
        return -1;
    }

    char df[4096];
    size_t n = 0;
    if (vfs_read_file(dfpath, df, sizeof(df) - 1, &n) != 0) {
        log_append(log, log_cap, "cannot read Dockerfile");
        return -1;
    }
    df[n] = '\0';

    char msg[128];
    snprintf(msg, sizeof(msg), "building %s from %s", tag, context_dir);
    log_append(log, log_cap, msg);

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
                log_append(log, log_cap, msg);
                if (str_has(base, "nginx")) {
                    char html[CTR_PATH_MAX];
                    snprintf(html, sizeof(html), "%s/usr/share/nginx/html", rootfs);
                    vfs_mkdir(html);
                    log_append(log, log_cap,
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
                log_append(log, log_cap, "COPY needs src dest");
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
            if (parse_word(&lp, pw, sizeof(pw)) == 0 && parse_port(pw)) {
                snprintf(expose_port, sizeof(expose_port), "%s", pw);
                snprintf(msg, sizeof(msg), "EXPOSE %s", pw);
            } else {
                snprintf(msg, sizeof(msg), "skip: %s", line);
            }
            log_append(log, log_cap, msg);
            continue;
        }

        if (!strncmp(lp, "WORKDIR", 7) || !strncmp(lp, "CMD", 3) ||
            !strncmp(lp, "ENV", 3) ||
            !strncmp(lp, "RUN", 3) || !strncmp(lp, "ENTRYPOINT", 10)) {
            snprintf(msg, sizeof(msg), "skip: %s", line);
            log_append(log, log_cap, msg);
            continue;
        }

        snprintf(msg, sizeof(msg), "unknown: %s", line);
        log_append(log, log_cap, msg);
    }

    char meta[CTR_PATH_MAX];
    image_meta_path(tag, meta, sizeof(meta));
    char metabuf[128];
    snprintf(metabuf, sizeof(metabuf), "%s\nexpose=%s\n", tag, expose_port);
    vfs_write_file(meta, metabuf, strlen(metabuf));

    snprintf(last_image, sizeof(last_image), "%s", tag);
    snprintf(last_port, sizeof(last_port), "%s", expose_port);

    snprintf(msg, sizeof(msg), "ok - %d COPY step(s), image %s", copies, tag);
    log_append(log, log_cap, msg);
    if (from_quarantined)
        log_append(log, log_cap,
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
    image_meta_path(tag, meta, sizeof(meta));
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

static int try_read(const char *path, char *body, size_t body_cap, size_t *n) {
    if (!vfs_is_file(path))
        return -1;
    return vfs_read_file(path, body, body_cap > 0 ? body_cap - 1 : 0, n);
}

static int resolve_rootfs_file(const char *rootfs, const char *path,
                               char *found, size_t found_cap, char *body,
                               size_t body_cap, size_t *n_out) {
    char candidates[5][CTR_PATH_MAX];
    int nc = 0;

    if (!strcmp(path, "/") || path[0] == '\0') {
        snprintf(candidates[nc++], CTR_PATH_MAX, "%s/usr/share/nginx/html/index.html",
                 rootfs);
        snprintf(candidates[nc++], CTR_PATH_MAX, "%s/index.html", rootfs);
        snprintf(candidates[nc++], CTR_PATH_MAX, "%s/html/index.html", rootfs);
    } else {
        snprintf(candidates[nc++], CTR_PATH_MAX, "%s%s", rootfs, path);
        snprintf(candidates[nc++], CTR_PATH_MAX, "%s/usr/share/nginx/html%s",
                 rootfs, path);
        size_t pl = strlen(path);
        if (pl > 0 && path[pl - 1] == '/') {
            snprintf(candidates[nc++], CTR_PATH_MAX, "%s%sindex.html", rootfs, path);
        } else {
            snprintf(candidates[nc++], CTR_PATH_MAX, "%s%s/index.html", rootfs, path);
        }
    }

    size_t n = 0;
    for (int i = 0; i < nc; i++) {
        if (try_read(candidates[i], body, body_cap, &n) == 0) {
            if (found && found_cap)
                snprintf(found, found_cap, "%s", candidates[i]);
            if (n_out)
                *n_out = n;
            if (body_cap)
                body[n < body_cap ? n : body_cap - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int ctr_run(const char *image, const char *name, const char *port,
            char *log, size_t log_cap) {
    ctr_init();
    if (log && log_cap)
        log[0] = '\0';
    if (!image || !name || !port)
        return -1;
    /* Explicit ctr run = localhost listen consent (not LAN). */
    extern void privacy_grant_net_listen(int lan, int remember);
    extern void privacy_set_listeners_localhost_only(int on);
    privacy_set_listeners_localhost_only(1);
    privacy_grant_net_listen(0, 0);

    uint16_t port_num = parse_port(port);
    if (!port_num) {
        log_append(log, log_cap, "invalid port");
        return -1;
    }

    char rootfs[CTR_PATH_MAX];
    image_rootfs_path(image, rootfs, sizeof(rootfs));
    if (!vfs_is_dir(rootfs)) {
        log_append(log, log_cap, "image not found — run ctr build first");
        return -1;
    }

    if (find_by_port(port)) {
        struct ctr_container *busy = find_by_port(port);
        if (!name || strcmp(busy->name, name) != 0) {
            log_append(log, log_cap, "port already in use");
            return -1;
        }
    }

    struct ctr_container *c = find_by_name(name);
    if (!c) {
        c = alloc_slot();
        if (!c) {
            log_append(log, log_cap, "too many containers");
            return -1;
        }
        memset(c, 0, sizeof(*c));
        c->used = 1;
        c->listen_id = -1;
        snprintf(c->name, sizeof(c->name), "%s", name);
    }

    if (c->running && c->listen_id >= 0)
        net_tcp_unlisten(c->port_num);

    int lid = net_tcp_listen(port_num);
    if (lid < 0) {
        log_append(log, log_cap, "TCP listen failed");
        return -1;
    }

    snprintf(c->image, sizeof(c->image), "%s", image);
    snprintf(c->port, sizeof(c->port), "%s", port);
    snprintf(c->rootfs, sizeof(c->rootfs), "%s", rootfs);
    c->port_num = port_num;
    c->listen_id = lid;
    c->running = 1;
    c->log[0] = '\0';
    log_append(c->log, sizeof(c->log), "container started (in-guest Peak runtime)");

    struct net_info ni;
    net_get_info(&ni);
    char ipb[32];
    net_format_ip(ni.ip, ipb, sizeof(ipb));
    char msg[160];
    snprintf(msg, sizeof(msg), "listening on http://%s:%s/ (and 127.0.0.1)",
             ni.up && ni.ip ? ipb : "0.0.0.0", port);
    log_append(c->log, sizeof(c->log), msg);

    if (log && log_cap)
        snprintf(log, log_cap, "%s", c->log);
    return 0;
}

int ctr_stop(const char *name) {
    ctr_init();
    struct ctr_container *c = find_by_name(name);
    if (!c)
        return -1;
    if (c->running && c->port_num)
        net_tcp_unlisten(c->port_num);
    c->running = 0;
    c->listen_id = -1;
    log_append(c->log, sizeof(c->log), "stopped");
    return 0;
}

int ctr_ps(char *out, size_t out_cap) {
    ctr_init();
    if (!out || !out_cap)
        return -1;
    size_t o = 0;
    o += (size_t)snprintf(out + o, out_cap - o, "NAME\tIMAGE\tPORT\tSTATUS\n");
    for (int i = 0; i < CTR_MAX_CONTAINERS && o + 1 < out_cap; i++) {
        if (!containers[i].used)
            continue;
        const char *st = "Exited";
        if (containers[i].running) {
            st = net_tcp_listening(containers[i].port_num) ? "Up/listen" : "Up";
        }
        o += (size_t)snprintf(out + o, out_cap - o, "%s\t%s\t%s\t%s\n",
                              containers[i].name, containers[i].image,
                              containers[i].port, st);
    }
    return 0;
}

int ctr_logs(const char *name, char *out, size_t out_cap) {
    ctr_init();
    struct ctr_container *c = find_by_name(name);
    if (!c || !out || !out_cap)
        return -1;
    snprintf(out, out_cap, "%s", c->log);
    return 0;
}

int ctr_ping(char *out, size_t out_cap) {
    ctr_init();
    if (out && out_cap)
        snprintf(out, out_cap, "{\"ok\":true,\"runtime\":\"peak-in-guest\"}");
    return 0;
}

static void serve_http_conn(struct ctr_container *c, int fd) {
    char req[CTR_HTTP_REQ_MAX];
    size_t total = 0;
    uint64_t start = 0;
    (void)start;
    for (int spins = 0; spins < 40 && total + 1 < sizeof(req); spins++) {
        size_t n = 0;
        if (net_tcp_fd_recv(fd, req + total, sizeof(req) - 1 - total, &n, 2) != 0) {
            if (total > 0)
                break;
            continue;
        }
        total += n;
        req[total] = '\0';
        if (contains(req, "\r\n\r\n") || contains(req, "\n\n"))
            break;
    }
    req[total] = '\0';

    char method[16], raw_path[CTR_PATH_MAX], path[CTR_PATH_MAX];
    if (http_parse_request_line(req, method, sizeof(method), raw_path,
                                sizeof(raw_path)) != 0) {
        const char *bad =
            "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n";
        net_tcp_fd_send(fd, bad, strlen(bad));
        net_tcp_fd_close(fd);
        log_append(c->log, sizeof(c->log), "HTTP 400");
        return;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        const char *bad =
            "HTTP/1.0 405 Method Not Allowed\r\nAllow: GET, HEAD\r\n"
            "Connection: close\r\n\r\n";
        net_tcp_fd_send(fd, bad, strlen(bad));
        net_tcp_fd_close(fd);
        log_append(c->log, sizeof(c->log), "HTTP 405");
        return;
    }

    if (http_normalize_path(raw_path, path, sizeof(path)) != 0) {
        const char *bad =
            "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n"
            "bad path\n";
        net_tcp_fd_send(fd, bad, strlen(bad));
        net_tcp_fd_close(fd);
        log_append(c->log, sizeof(c->log), "HTTP 400 path");
        return;
    }

    static char body[CTR_HTTP_BODY_MAX];
    size_t blen = 0;
    char found[CTR_PATH_MAX];
    int ok = resolve_rootfs_file(c->rootfs, path, found, sizeof(found), body,
                                 sizeof(body), &blen);

    char hdr[384];
    if (ok != 0) {
        const char *msg =
            "<html><body><h1>404</h1><p>not found</p></body></html>";
        snprintf(hdr, sizeof(hdr),
                 "HTTP/1.0 404 Not Found\r\n"
                 "Content-Type: text/html; charset=utf-8\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 (unsigned)strlen(msg));
        net_tcp_fd_send(fd, hdr, strlen(hdr));
        if (strcmp(method, "HEAD") != 0)
            net_tcp_fd_send(fd, msg, strlen(msg));
        log_append(c->log, sizeof(c->log), "GET -> 404");
    } else {
        const char *mime;
        if (!strcmp(path, "/") ||
            (path[0] && path[strlen(path) - 1] == '/'))
            mime = "text/html; charset=utf-8";
        else
            mime = http_mime_for_path(path);
        snprintf(hdr, sizeof(hdr),
                 "HTTP/1.0 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 mime, (unsigned)blen);
        net_tcp_fd_send(fd, hdr, strlen(hdr));
        if (strcmp(method, "HEAD") != 0 && blen)
            net_tcp_fd_send(fd, body, blen);
        char msg[160];
        snprintf(msg, sizeof(msg), "GET %s -> 200 (%u bytes)", path, (unsigned)blen);
        log_append(c->log, sizeof(c->log), msg);
    }
    net_tcp_fd_close(fd);
}

void ctr_poll(void) {
    if (!ctr_ready)
        return;
    for (int i = 0; i < CTR_MAX_CONTAINERS; i++) {
        struct ctr_container *c = &containers[i];
        if (!c->used || !c->running || c->listen_id < 0)
            continue;
        for (int n = 0; n < 4; n++) {
            int fd = net_tcp_accept(c->listen_id);
            if (fd < 0)
                break;
            serve_http_conn(c, fd);
        }
    }
}

int ctr_http_get(const char *url, char *body, size_t body_cap, int *status_out) {
    ctr_init();
    if (body_cap)
        body[0] = '\0';
    if (status_out)
        *status_out = 0;
    if (!url)
        return -1;

    const char *p = url;
    if (!strncmp(p, "http://", 7))
        p += 7;
    else if (!strncmp(p, "https://", 8))
        p += 8;

    char host[64];
    char port[16];
    char path_raw[CTR_PATH_MAX];
    size_t hi = 0;
    while (*p && *p != ':' && *p != '/' && hi + 1 < sizeof(host))
        host[hi++] = *p++;
    host[hi] = '\0';

    port[0] = '\0';
    if (*p == ':') {
        p++;
        size_t pi = 0;
        while (*p && *p != '/' && pi + 1 < sizeof(port))
            port[pi++] = *p++;
        port[pi] = '\0';
    } else {
        snprintf(port, sizeof(port), "80");
    }

    if (*p == '/') {
        size_t qi = 0;
        while (*p && *p != '?' && qi + 1 < sizeof(path_raw))
            path_raw[qi++] = *p++;
        path_raw[qi] = '\0';
    } else {
        snprintf(path_raw, sizeof(path_raw), "/");
    }

    char path[CTR_PATH_MAX];
    if (http_normalize_path(path_raw, path, sizeof(path)) != 0) {
        if (status_out)
            *status_out = 400;
        if (body_cap)
            snprintf(body, body_cap, "bad path");
        return -1;
    }

    struct ctr_container *c = find_by_port(port);
    if (!c) {
        if (status_out)
            *status_out = 502;
        if (body_cap)
            snprintf(body, body_cap, "no container on port %s — try: ctr run", port);
        return -1;
    }

    size_t n = 0;
    if (resolve_rootfs_file(c->rootfs, path, NULL, 0, body, body_cap, &n) == 0) {
        if (status_out)
            *status_out = 200;
        char msg[160];
        snprintf(msg, sizeof(msg), "GET %s -> 200", path);
        log_append(c->log, sizeof(c->log), msg);
        (void)host;
        return 0;
    }

    if (status_out)
        *status_out = 404;
    if (body_cap)
        snprintf(body, body_cap, "<html><body><h1>404</h1><p>%s not found</p></body></html>",
                 path);
    log_append(c->log, sizeof(c->log), "GET -> 404");
    return -1;
}
