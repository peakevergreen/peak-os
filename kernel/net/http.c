#include "net.h"
#include "net_internal.h"
#include "tls.h"
#include "cap.h"
#include "timer.h"
#include "util.h"

int net_http_needs_tls(void) {
    return http_needs_tls_flag;
}

static int parse_url(const char *url, int *https, char *host, size_t host_cap,
                     uint16_t *port, char *path, size_t path_cap) {
    const char *p = url;
    *https = 0;
    *port = 80;
    if (!strncmp(p, "https://", 8)) {
        *https = 1;
        *port = 443;
        p += 8;
    } else if (!strncmp(p, "http://", 7)) {
        p += 7;
    }
    size_t hi = 0;
    while (*p && *p != '/' && *p != ':' && hi + 1 < host_cap)
        host[hi++] = *p++;
    host[hi] = '\0';
    if (*p == ':') {
        p++;
        uint16_t po = 0;
        while (*p >= '0' && *p <= '9') {
            po = (uint16_t)(po * 10 + (*p - '0'));
            p++;
        }
        *port = po;
    }
    if (*p == '/')
        snprintf(path, path_cap, "%s", p);
    else
        snprintf(path, path_cap, "/");
    return host[0] ? 0 : -1;
}

static int header_value(const char *raw, const char *key, char *out, size_t out_cap) {
    size_t klen = strlen(key);
    const char *p = raw;
    while (*p && !(*p == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')) {
        /* start of line */
        int match = 1;
        for (size_t i = 0; i < klen; i++) {
            char a = p[i], b = key[i];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z')
                b = (char)(b - 'A' + 'a');
            if (a != b) {
                match = 0;
                break;
            }
        }
        if (match && p[klen] == ':') {
            p += klen + 1;
            while (*p == ' ' || *p == '\t')
                p++;
            size_t o = 0;
            while (*p && *p != '\r' && *p != '\n' && o + 1 < out_cap)
                out[o++] = *p++;
            out[o] = '\0';
            return 0;
        }
        while (*p && !(*p == '\r' && p[1] == '\n'))
            p++;
        if (*p == '\r' && p[1] == '\n')
            p += 2;
        else
            break;
    }
    return -1;
}

static void copy_response_headers(char *buf, char *hdr_out, size_t hdr_cap) {
    if (!hdr_out || hdr_cap < 2)
        return;
    hdr_out[0] = '\0';
    if (!buf)
        return;
    size_t total = strlen(buf);
    for (size_t i = 0; i + 3 < total; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' &&
            buf[i + 3] == '\n') {
            size_t hlen = i + 4;
            if (hlen >= hdr_cap)
                hlen = hdr_cap - 1;
            memcpy(hdr_out, buf, hlen);
            hdr_out[hlen] = '\0';
            return;
        }
    }
}

static void strip_http_headers(char *body) {
    size_t total = strlen(body);
    for (size_t i = 0; i + 3 < total; i++) {
        if (body[i] == '\r' && body[i + 1] == '\n' && body[i + 2] == '\r' &&
            body[i + 3] == '\n') {
            char *hdr_end = body + i + 4;
            size_t blen = strlen(hdr_end);
            memmove(body, hdr_end, blen + 1);
            return;
        }
    }
}

static void tls_fail_page(char *body, size_t body_cap, const char *host, const char *why) {
    snprintf(body, body_cap,
             "<html><head><title>TLS failed</title>"
             "<style>body{background:#0B1A12;color:#E8F0EA}h1{color:#C45C5C}"
             "code{color:#9AC4AE}</style></head><body>"
             "<h1>TLS handshake failed</h1>"
             "<p>Host: <code>%s</code></p>"
             "<p>%s</p>"
             "<p>Peak TLS 1.2: ECDHE + AES-128-GCM or ChaCha20-Poly1305 (X25519).</p>"
             "</body></html>",
             host, why ? why : "unknown error");
}

static int build_http_request(char *req, size_t req_cap, const char *method,
                              const char *path, const char *host,
                              const char *extra_headers, const char *body,
                              size_t body_len, int tls) {
    size_t off = 0;
    const char *m = method && method[0] ? method : "GET";
    off = (size_t)snprintf(req, req_cap, "%s %s HTTP/1.0\r\nHost: %s\r\n", m, path, host);
    if (tls)
        off += (size_t)snprintf(req + off, req_cap - off,
                                "User-Agent: PeakBrowser/1\r\n");
    else
        off += (size_t)snprintf(req + off, req_cap - off,
                                "User-Agent: PeakBrowser/1\r\n");
    off += (size_t)snprintf(req + off, req_cap - off,
                            "Accept: text/html,application/xhtml+xml,*/*;q=0.8\r\n"
                            "Accept-Encoding: identity\r\n"
                            "Connection: close\r\n");
    if (extra_headers && extra_headers[0]) {
        off += (size_t)snprintf(req + off, req_cap - off, "%s", extra_headers);
        if (off >= 2 && req[off - 1] != '\n')
            off += (size_t)snprintf(req + off, req_cap - off, "\r\n");
    }
    if (body && body_len > 0) {
        off += (size_t)snprintf(req + off, req_cap - off, "Content-Length: %zu\r\n",
                                body_len);
    }
    if (off + 4 >= req_cap)
        return -1;
    memcpy(req + off, "\r\n", 2);
    off += 2;
    if (body && body_len > 0) {
        if (off + body_len >= req_cap)
            return -1;
        memcpy(req + off, body, body_len);
        off += body_len;
    }
    req[off] = '\0';
    return 0;
}

static int parse_http_status(const char *buf, int *status_out) {
    int st = 0;
    if (!strncmp(buf, "HTTP/", 5)) {
        const char *s = buf;
        while (*s && *s != ' ')
            s++;
        while (*s == ' ')
            s++;
        while (*s >= '0' && *s <= '9') {
            st = st * 10 + (*s - '0');
            s++;
        }
    }
    if (status_out)
        *status_out = st;
    return st;
}

static int recv_http_response_tls(char *buf, size_t buf_cap) {
    size_t total = 0;
    uint64_t last_progress = timer_ticks();
    while (total + 1 < buf_cap) {
        size_t n = 0;
        if (tls_recv(buf + total, buf_cap - 1 - total, &n, 100) != 0) {
            /* Stream done (close_notify/alert or TCP torn down) — not a stall. */
            if (!tls_ready())
                break;
            if (tcp_got_fin || tcp_state == TCP_CLOSED || tcp_state == TCP_CLOSE_WAIT)
                break;
            /* Mid-transfer stall (retransmits, slow origin): keep waiting up
             * to 12 s without progress instead of truncating the page. */
            if (timer_ticks() - last_progress > 1200)
                break;
            continue;
        }
        total += n;
        last_progress = timer_ticks();
    }
    buf[total] = '\0';
    return total > 0 ? 0 : -4;
}

static int recv_http_response_tcp(char *buf, size_t buf_cap) {
    size_t total = 0;
    uint64_t start = timer_ticks();
    while (total + 1 < buf_cap && timer_ticks() - start < 800) {
        size_t n = 0;
        if (net_tcp_recv(buf + total, buf_cap - 1 - total, &n, 100) != 0) {
            if (tcp_got_fin || tcp_state == TCP_CLOSED || tcp_state == TCP_CLOSE_WAIT)
                break;
            continue;
        }
        total += n;
        start = timer_ticks();
    }
    buf[total] = '\0';
    return total > 0 ? 0 : -1;
}

/* HTTP over TLS; leaves full response in buf. */
static int https_exchange_raw(uint32_t ip, const char *host, const char *path,
                              const char *method, const char *extra_headers,
                              const char *body, size_t body_len, char *buf, size_t buf_cap,
                              int *status_out) {
    if (tls_connect(ip, 443, host, 1200) != 0)
        return -2;

    char req[2048];
    if (build_http_request(req, sizeof(req), method, path, host, extra_headers, body,
                           body_len, 1) != 0) {
        tls_close();
        return -3;
    }
    if (tls_send(req, strlen(req)) != 0) {
        tls_close();
        return -3;
    }

    int ex = recv_http_response_tls(buf, buf_cap);
    tls_close();
    if (ex != 0)
        return ex;
    parse_http_status(buf, status_out);
    return 0;
}

/* Exchange; leaves full response (headers+body) in buf. */
static int http_exchange_raw(uint32_t ip, uint16_t port, const char *host, const char *path,
                             const char *method, const char *extra_headers, const char *body,
                             size_t body_len, char *buf, size_t buf_cap, int *status_out) {
    if (net_tcp_connect(ip, port, 500) != 0)
        return -1;

    char req[2048];
    if (build_http_request(req, sizeof(req), method, path, host, extra_headers, body,
                           body_len, 0) != 0) {
        net_tcp_close();
        return -1;
    }
    if (net_tcp_send(req, strlen(req)) != 0) {
        net_tcp_close();
        return -1;
    }

    int ex = recv_http_response_tcp(buf, buf_cap);
    net_tcp_close();
    if (ex != 0)
        return ex;
    parse_http_status(buf, status_out);
    return 0;
}

static int is_redirect(int st) {
    return st == 301 || st == 302 || st == 303 || st == 307 || st == 308;
}

static void join_redirect(const char *scheme_host_prefix, const char *host, const char *cur_path,
                          const char *loc, char *out, size_t out_cap) {
    if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8)) {
        snprintf(out, out_cap, "%s", loc);
        return;
    }
    if (loc[0] == '/') {
        snprintf(out, out_cap, "%s%s%s", scheme_host_prefix, host, loc);
        return;
    }
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", cur_path);
    char *slash = NULL;
    for (char *p = dir; *p; p++)
        if (*p == '/')
            slash = p;
    if (slash)
        slash[1] = '\0';
    else
        snprintf(dir, sizeof(dir), "/");
    snprintf(out, out_cap, "%s%s%s%s", scheme_host_prefix, host, dir, loc);
}

int net_http_request(const struct net_http_request *req, char *body, size_t body_cap,
                     int *status_out, char *hdr_out, size_t hdr_cap) {
    attempt_stats.http++;
    if (!net_ready() || !req || !req->url || !body || !body_cap)
        return -1;
    if (!privacy_net_client_allowed())
        return -1;

    const char *method = req->method[0] ? req->method : "GET";
    if (strcmp(method, "GET") && strcmp(method, "POST"))
        return -1;

    http_needs_tls_flag = 0;
    char cur[320];
    snprintf(cur, sizeof(cur), "%s", req->url);

    for (int hop = 0; hop < 5; hop++) {
        int https = 0;
        char host[128], path[256];
        uint16_t port = 80;
        if (parse_url(cur, &https, host, sizeof(host), &port, path, sizeof(path)) != 0)
            return -1;

        uint32_t ip = net_dns_resolve(host, 300);
        if (!ip) {
            if (status_out)
                *status_out = 0;
            snprintf(body, body_cap,
                     "<html><body><h1>DNS failed</h1><p>Could not resolve %s</p></body></html>",
                     host);
            return -1;
        }

        int st = 0;
        int ex;
        const char *send_body = req->body;
        size_t send_len = req->body_len;
        if (hop > 0) {
            send_body = NULL;
            send_len = 0;
        }
        if (https || port == 443) {
            http_needs_tls_flag = 1;
            ex = https_exchange_raw(ip, host, path, method, req->headers, send_body,
                                    send_len, body, body_cap, &st);
            if (ex != 0) {
                if (status_out)
                    *status_out = 0;
                const char *why = "Handshake or empty response";
                if (ex == -2)
                    why = tls_last_error();
                else if (ex == -3)
                    why = "TLS connected but request send failed";
                else if (ex == -4)
                    why = "TLS connected but empty HTTP response";
                tls_fail_page(body, body_cap, host, why);
                return -1;
            }
        } else {
            ex = http_exchange_raw(ip, port, host, path, method, req->headers, send_body,
                                   send_len, body, body_cap, &st);
            if (ex != 0) {
                if (status_out)
                    *status_out = 0;
                return -1;
            }
        }
        if (status_out)
            *status_out = st;

        if (is_redirect(st)) {
            char loc[280];
            if (header_value(body, "location", loc, sizeof(loc)) != 0) {
                copy_response_headers(body, hdr_out, hdr_cap);
                strip_http_headers(body);
                return -1;
            }
            const char *pref = (https || port == 443) ? "https://" : "http://";
            char next[320];
            join_redirect(pref, host, path, loc, next, sizeof(next));
            snprintf(cur, sizeof(cur), "%s", next);
            continue;
        }

        copy_response_headers(body, hdr_out, hdr_cap);

        if (st >= 200 && st < 300) {
            strip_http_headers(body);
            http_needs_tls_flag = 0;
            return 0;
        }

        strip_http_headers(body);
        if (!body[0]) {
            snprintf(body, body_cap,
                     "<html><body><h1>HTTP %d</h1><p>Empty response from %s</p></body></html>",
                     st, host);
        }
        return -1;
    }

    if (status_out)
        *status_out = 0;
    snprintf(body, body_cap, "<html><body><h1>Too many redirects</h1></body></html>");
    return -1;
}

int net_http_get(const char *url, char *body, size_t body_cap, int *status_out) {
    struct net_http_request req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    req.url = url;
    return net_http_request(&req, body, body_cap, status_out, NULL, 0);
}
