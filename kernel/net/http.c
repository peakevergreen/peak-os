#include "net.h"
#include "net_internal.h"
#include "http_util.h"
#include "tls.h"
#include "tls_hsts.h"
#include "cap.h"
#include "privacy.h"
#include "timer.h"
#include "util.h"

static int last_tls_secure;
static int last_tls_verified;

int net_http_needs_tls(void) {
    return http_needs_tls_flag;
}

int net_http_last_tls_secure(void) {
    return last_tls_secure;
}

int net_http_last_tls_verified(void) {
    return last_tls_verified;
}

/* Stable webapi / UI names for the last TLS failure. */
const char *net_http_tls_reject_name(void) {
    int code = tls_last_error_code();
    const char *why = tls_last_error();
    if (code == TLS_E_RNG)
        return "fetch: tls-rng";
    if (code == TLS_E_ALERT)
        return "fetch: tls-alert";
    if (why && strstr(why, "expired"))
        return "fetch: tls-expired";
    if (why && strstr(why, "hostname mismatch"))
        return "fetch: tls-mismatch";
    if (why && (strstr(why, "Untrusted") || strstr(why, "untrusted") ||
                strstr(why, "WebPKI") || strstr(why, "changed")))
        return "fetch: tls-untrusted";
    if (code == TLS_E_CERT)
        return "fetch: tls-untrusted";
    return "fetch: tls-handshake";
}

static void tls_fail_page(char *body, size_t body_cap, const char *host, const char *why) {
    int code = tls_last_error_code();
    const char *title = "TLS handshake failed";
    const char *detail = why ? why : "unknown error";
    if (code == TLS_E_RNG) {
        title = "Secure connection unavailable — RNG not ready";
    } else if (code == TLS_E_ALERT) {
        title = "Server rejected the connection";
    } else if (why && strstr(why, "expired")) {
        title = "Certificate expired or not yet valid";
    } else if (why && strstr(why, "hostname mismatch")) {
        title = "Certificate hostname mismatch";
    } else if (why && (strstr(why, "Untrusted") || strstr(why, "WebPKI") ||
                       strstr(why, "changed"))) {
        title = "Untrusted certificate";
    } else if (code == TLS_E_CERT) {
        title = "Untrusted certificate";
    }
    snprintf(body, body_cap,
             "<html><head><title>%s</title>"
             "<style>body{background:#0B1A12;color:#E8F0EA}h1{color:#C45C5C}"
             "code{color:#9AC4AE}</style></head><body>"
             "<h1>%s</h1>"
             "<p>Host: <code>%s</code></p>"
             "<p>%s</p>"
             "<p>Peak TLS 1.2/1.3 with WebPKI (pins override; TOFU opt-in).</p>"
             "</body></html>",
             title, title, host, detail);
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

static int recv_http_response_tls(char *buf, size_t buf_cap) {
    size_t total = 0;
    uint64_t last_progress = timer_ticks();
    while (total + 1 < buf_cap) {
        size_t n = 0;
        if (tls_recv(buf + total, buf_cap - 1 - total, &n, NET_TCP_RECV_SLICE_TICKS) != 0) {
            /* Stream done (close_notify/alert or TCP torn down) — not a stall. */
            if (!tls_ready())
                break;
            if (tcp_got_fin || tcp_state == TCP_CLOSED || tcp_state == TCP_CLOSE_WAIT)
                break;
            /* Mid-transfer stall (retransmits, slow origin): keep waiting up
             * to NET_HTTP_IDLE_TLS_TICKS without progress instead of truncating. */
            if (net_timed_out(last_progress, NET_HTTP_IDLE_TLS_TICKS))
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
    while (total + 1 < buf_cap && !net_timed_out(start, NET_HTTP_IDLE_TCP_TICKS)) {
        size_t n = 0;
        if (net_tcp_recv(buf + total, buf_cap - 1 - total, &n, NET_TCP_RECV_SLICE_TICKS) != 0) {
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
    last_tls_secure = 0;
    last_tls_verified = 0;
    if (tls_connect(ip, 443, host, NET_TLS_HANDSHAKE_TICKS) != 0)
        return -2;

    last_tls_secure = 1;
    last_tls_verified = tls_cert_verified() && tls_hostname_matched();

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
        return -4;
    http_parse_status(buf, status_out);
    return 0;
}

/* Exchange; leaves full response (headers+body) in buf. */
static int http_exchange_raw(uint32_t ip, uint16_t port, const char *host, const char *path,
                             const char *method, const char *extra_headers, const char *body,
                             size_t body_len, char *buf, size_t buf_cap, int *status_out) {
    if (net_tcp_connect(ip, port, NET_TCP_CONNECT_HTTP_TICKS) != 0)
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
    http_parse_status(buf, status_out);
    return 0;
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
        if (http_parse_url(cur, &https, host, sizeof(host), &port, path, sizeof(path)) != 0)
            return -1;

        /* HSTS-lite: upgrade cached hosts to HTTPS. */
        if (!https && hsts_should_upgrade(host)) {
            char upgraded[320];
            snprintf(upgraded, sizeof(upgraded), "https://%s%s", host, path[0] ? path : "/");
            snprintf(cur, sizeof(cur), "%s", upgraded);
            if (http_parse_url(cur, &https, host, sizeof(host), &port, path, sizeof(path)) != 0)
                return -1;
        }

        uint32_t ip = net_dns_resolve(host, NET_DNS_RESOLVE_TICKS);
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

        if (http_is_redirect(st)) {
            char loc[280];
            if (http_header_value(body, "location", loc, sizeof(loc)) != 0) {
                http_copy_response_headers(body, hdr_out, hdr_cap);
                http_strip_headers(body);
                return -1;
            }
            char next[320];
            if (http_join_redirect(https || port == 443, host, port, path, loc, next,
                                   sizeof(next)) != 0) {
                http_copy_response_headers(body, hdr_out, hdr_cap);
                http_strip_headers(body);
                return -1;
            }
            snprintf(cur, sizeof(cur), "%s", next);
            continue;
        }

        http_copy_response_headers(body, hdr_out, hdr_cap);

        if (st >= 200 && st < 300) {
            if ((https || port == 443) && hdr_out)
                hsts_process_header(host, hdr_out);
            else if (https || port == 443) {
                /* Headers may only live in body before strip. */
                char hdrtmp[2048];
                http_copy_response_headers(body, hdrtmp, sizeof(hdrtmp));
                hsts_process_header(host, hdrtmp);
            }
            http_strip_headers(body);
            http_needs_tls_flag = 0;
            return 0;
        }

        http_strip_headers(body);
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
