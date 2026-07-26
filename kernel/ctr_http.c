#include "ctr.h"
#include "ctr_internal.h"
#include "http_util.h"
#include "net.h"
#include "util.h"
#include "vfs.h"

static int try_read(const char *path, char *body, size_t body_cap, size_t *n) {
    if (!vfs_is_file(path))
        return -1;
    return vfs_read_file(path, body, body_cap > 0 ? body_cap - 1 : 0, n);
}

static int resolve_rootfs_file(const char *rootfs, const char *path,
                               char *found, size_t found_cap, char *body,
                               size_t body_cap, size_t *n_out) {
    char candidates[CTR_ROOTFS_CANDS][CTR_PATH_MAX];
    int nc = ctr_resolve_rootfs_candidates(rootfs, path, candidates,
                                           CTR_ROOTFS_CANDS);
    if (nc < 0)
        return -1;

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

static int parse_content_range(const char *req, size_t total, size_t *start, size_t *end) {
    if (!req || !start || !end)
        return -1;
    const char *p = req;
    while (*p) {
        if ((p[0] == 'R' || p[0] == 'r') &&
            (p[1] == 'a' || p[1] == 'A') &&
            (p[2] == 'n' || p[2] == 'N') &&
            (p[3] == 'g' || p[3] == 'G') &&
            (p[4] == 'e' || p[4] == 'E') && p[5] == ':') {
            p += 6;
            while (*p == ' ')
                p++;
            if (strncmp(p, "bytes=", 6))
                return -1;
            p += 6;
            *start = 0;
            *end = total ? total - 1 : 0;
            size_t a = 0;
            while (*p >= '0' && *p <= '9')
                a = a * 10 + (size_t)(*p++ - '0');
            *start = a;
            if (*p == '-') {
                p++;
                if (*p >= '0' && *p <= '9') {
                    size_t b = 0;
                    while (*p >= '0' && *p <= '9')
                        b = b * 10 + (size_t)(*p++ - '0');
                    *end = b;
                } else if (*p == '\0' || *p == '\r' || *p == '\n') {
                    *end = total ? total - 1 : 0;
                }
            }
            if (*start >= total)
                return -1;
            if (*end >= total)
                *end = total - 1;
            if (*end < *start)
                return -1;
            return 0;
        }
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    return -1;
}

static void serve_http_conn(struct ctr_container *c, int fd) {
    char req[CTR_HTTP_REQ_MAX];
    size_t total = 0;
    for (int spins = 0; spins < 40 && total + 1 < sizeof(req); spins++) {
        size_t n = 0;
        if (net_tcp_fd_recv(fd, req + total, sizeof(req) - 1 - total, &n, 2) != 0) {
            if (total > 0)
                break;
            continue;
        }
        total += n;
        req[total] = '\0';
        if (ctr_str_has(req, "\r\n\r\n") || ctr_str_has(req, "\n\n"))
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
        ctr_log_append(c->log, sizeof(c->log), "HTTP 400");
        return;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        const char *bad =
            "HTTP/1.0 405 Method Not Allowed\r\nAllow: GET, HEAD\r\n"
            "Connection: close\r\n\r\n";
        net_tcp_fd_send(fd, bad, strlen(bad));
        net_tcp_fd_close(fd);
        ctr_log_append(c->log, sizeof(c->log), "HTTP 405");
        return;
    }

    if (http_normalize_path(raw_path, path, sizeof(path)) != 0) {
        const char *bad =
            "HTTP/1.0 400 Bad Request\r\nConnection: close\r\n\r\n"
            "bad path\n";
        net_tcp_fd_send(fd, bad, strlen(bad));
        net_tcp_fd_close(fd);
        ctr_log_append(c->log, sizeof(c->log), "HTTP 400 path");
        return;
    }

    static char body[CTR_HTTP_BODY_MAX];
    size_t blen = 0;
    char found[CTR_PATH_MAX];
    int ok = resolve_rootfs_file(c->rootfs, path, found, sizeof(found), body,
                                 sizeof(body), &blen);
    if (ok != 0)
        ok = ctr_dir_index_listing(c->rootfs, path, body, sizeof(body), &blen);

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
        ctr_log_append(c->log, sizeof(c->log), "GET -> 404");
    } else {
        const char *mime;
        if (!strcmp(path, "/") ||
            (path[0] && path[strlen(path) - 1] == '/'))
            mime = "text/html; charset=utf-8";
        else
            mime = http_mime_for_path(path);
        size_t rs = 0, re = blen ? blen - 1 : 0;
        int partial = parse_content_range(req, blen, &rs, &re) == 0;
        size_t send_off = partial ? rs : 0;
        size_t send_len = partial ? (re - rs + 1) : blen;
        if (partial) {
            if (c->env_note[0])
                snprintf(hdr, sizeof(hdr),
                         "HTTP/1.0 206 Partial Content\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %u\r\n"
                         "Content-Range: bytes %zu-%zu/%u\r\n"
                         "Accept-Ranges: bytes\r\n"
                         "X-Peak-Env: %s\r\n"
                         "Connection: close\r\n\r\n",
                         mime, (unsigned)send_len, rs, re, (unsigned)blen, c->env_note);
            else
                snprintf(hdr, sizeof(hdr),
                         "HTTP/1.0 206 Partial Content\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %u\r\n"
                         "Content-Range: bytes %zu-%zu/%u\r\n"
                         "Accept-Ranges: bytes\r\n"
                         "Connection: close\r\n\r\n",
                         mime, (unsigned)send_len, rs, re, (unsigned)blen);
        } else if (c->env_note[0])
            snprintf(hdr, sizeof(hdr),
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "Accept-Ranges: bytes\r\n"
                     "X-Peak-Env: %s\r\n"
                     "Connection: close\r\n\r\n",
                     mime, (unsigned)blen, c->env_note);
        else
            snprintf(hdr, sizeof(hdr),
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n"
                     "Accept-Ranges: bytes\r\n"
                     "Connection: close\r\n\r\n",
                     mime, (unsigned)blen);
        net_tcp_fd_send(fd, hdr, strlen(hdr));
        if (strcmp(method, "HEAD") != 0 && send_len)
            net_tcp_fd_send(fd, body + send_off, send_len);
        char msg[160];
        snprintf(msg, sizeof(msg), "GET %s -> %s (%u bytes)", path,
                 partial ? "206" : "200", (unsigned)send_len);
        ctr_log_append(c->log, sizeof(c->log), msg);
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

    struct ctr_container *c = ctr_find_by_port(port);
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
        ctr_log_append(c->log, sizeof(c->log), msg);
        (void)host;
        return 0;
    }

    if (status_out)
        *status_out = 404;
    if (body_cap)
        snprintf(body, body_cap, "<html><body><h1>404</h1><p>%s not found</p></body></html>",
                 path);
    ctr_log_append(c->log, sizeof(c->log), "GET -> 404");
    return -1;
}
