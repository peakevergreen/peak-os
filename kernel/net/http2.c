/*
 * Minimal HTTP/2 GET client (RFC 7540 subset) for ALPN h2 sessions.
 * Preface + SETTINGS + stream-1 HEADERS; synthesizes an HTTP/1.0 response.
 */
#include "http2.h"
#include "tls.h"
#include "net_internal.h"
#include "timer.h"
#include "util.h"

static void wr24(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t rd24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int send_frame(uint8_t type, uint8_t flags, uint32_t stream, const uint8_t *payload,
                      size_t plen) {
    uint8_t hdr[9];
    wr24(hdr, (uint32_t)plen);
    hdr[3] = type;
    hdr[4] = flags;
    wr32(hdr + 5, stream & 0x7fffffffu);
    if (tls_send(hdr, 9) != 0)
        return -1;
    if (plen && tls_send(payload, plen) != 0)
        return -1;
    return 0;
}

static int recv_exact(uint8_t *buf, size_t need) {
    size_t got = 0;
    uint64_t last = timer_ticks();
    while (got < need) {
        size_t n = 0;
        if (tls_recv(buf + got, need - got, &n, NET_TCP_RECV_SLICE_TICKS) != 0) {
            if (!tls_ready() || net_timed_out(last, NET_HTTP_IDLE_TLS_TICKS))
                return -1;
            continue;
        }
        if (!n) {
            if (net_timed_out(last, NET_HTTP_IDLE_TLS_TICKS))
                return -1;
            continue;
        }
        got += n;
        last = timer_ticks();
    }
    return 0;
}

static int hpack_add_lit(uint8_t *buf, size_t cap, size_t *o, uint8_t name_idx,
                         const char *val) {
    size_t vlen = strlen(val);
    if (vlen > 127 || *o + 2 + vlen > cap)
        return -1;
    buf[(*o)++] = (uint8_t)(name_idx & 0x0f); /* Literal without indexing */
    buf[(*o)++] = (uint8_t)vlen;
    memcpy(buf + *o, val, vlen);
    *o += vlen;
    return 0;
}

static int parse_dec(const uint8_t *p, size_t n) {
    int v = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9')
            break;
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

/* Scan HPACK block for :status (static index 8 or literal). */
static int hpack_find_status(const uint8_t *p, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b = p[i++];
        if (b & 0x80) {
            uint8_t idx = b & 0x7f;
            if (idx == 8)
                return 200;
            if (idx == 9)
                return 204;
            if (idx == 10)
                return 206;
            if (idx == 11)
                return 304;
            if (idx == 12)
                return 400;
            if (idx == 13)
                return 404;
            if (idx == 14)
                return 500;
            continue;
        }
        if ((b & 0xf0) == 0x00) {
            uint8_t nidx = b & 0x0f;
            if (nidx == 0) {
                if (i >= len)
                    break;
                size_t nlen = p[i++] & 0x7f;
                if (i + nlen > len)
                    break;
                int is_status = (nlen == 7 && !memcmp(p + i, ":status", 7));
                i += nlen;
                if (i >= len)
                    break;
                size_t vlen = p[i++] & 0x7f;
                if (i + vlen > len)
                    break;
                if (is_status)
                    return parse_dec(p + i, vlen);
                i += vlen;
            } else {
                if (i >= len)
                    break;
                size_t vlen = p[i++] & 0x7f;
                if (i + vlen > len)
                    break;
                if (nidx == 8)
                    return parse_dec(p + i, vlen);
                i += vlen;
            }
            continue;
        }
        /* Incremental indexing / other — skip name/value coarsely */
        if ((b & 0xc0) == 0x40) {
            uint8_t nidx = b & 0x3f;
            if (nidx == 0) {
                if (i >= len)
                    break;
                size_t nlen = p[i++] & 0x7f;
                if (i + nlen > len)
                    break;
                i += nlen;
            }
            if (i >= len)
                break;
            size_t vlen = p[i++] & 0x7f;
            if (i + vlen > len)
                break;
            i += vlen;
            continue;
        }
        break;
    }
    return 0;
}

int http2_get(const char *host, const char *path, const char *extra_headers, char *out,
              size_t out_cap, int *status_out) {
    (void)extra_headers;
    if (!host || !path || !out || out_cap < 128)
        return -1;

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (tls_send(preface, sizeof(preface) - 1) != 0)
        return -1;
    if (send_frame(0x04, 0, 0, NULL, 0) != 0)
        return -1;

    uint8_t hpack[256];
    size_t ho = 0;
    hpack[ho++] = 0x82; /* :method GET */
    hpack[ho++] = 0x87; /* :scheme https */
    if (!strcmp(path, "/"))
        hpack[ho++] = 0x84;
    else if (hpack_add_lit(hpack, sizeof(hpack), &ho, 4, path) != 0)
        return -1;
    if (hpack_add_lit(hpack, sizeof(hpack), &ho, 1, host) != 0)
        return -1;
    if (send_frame(0x01, 0x05, 1, hpack, ho) != 0)
        return -1;

    int status = 0;
    uint8_t body_acc[12288];
    size_t body_len = 0;
    int stream_done = 0;
    uint64_t start = timer_ticks();

    while (!stream_done && !net_timed_out(start, NET_HTTP_IDLE_TLS_TICKS)) {
        uint8_t fh[9];
        if (recv_exact(fh, 9) != 0)
            break;
        uint32_t plen = rd24(fh);
        uint8_t type = fh[3];
        uint8_t flags = fh[4];
        uint32_t sid = rd32(fh + 5) & 0x7fffffffu;
        if (plen > sizeof(body_acc))
            return -1;
        uint8_t payload[12288];
        if (plen && recv_exact(payload, plen) != 0)
            return -1;

        if (type == 0x04) {
            if (!(flags & 0x01) && send_frame(0x04, 0x01, 0, NULL, 0) != 0)
                return -1;
            continue;
        }
        if (type == 0x06 || type == 0x08)
            continue;
        if (type == 0x07)
            break;
        if (sid != 1)
            continue;

        if (type == 0x01) {
            size_t off = 0;
            if (flags & 0x08) {
                if (!plen)
                    continue;
                off = 1 + payload[0];
            }
            if (flags & 0x20)
                off += 5;
            if (off < plen) {
                int st = hpack_find_status(payload + off, plen - off);
                if (st)
                    status = st;
            }
            if (!status)
                status = 200;
            if (flags & 0x01)
                stream_done = 1;
            continue;
        }
        if (type == 0x00) {
            size_t data_off = 0;
            size_t data_len = plen;
            if (flags & 0x08) {
                if (!plen)
                    continue;
                uint8_t pad = payload[0];
                data_off = 1;
                if (1 + pad > plen)
                    continue;
                data_len = plen - 1 - pad;
            }
            if (body_len + data_len > sizeof(body_acc))
                data_len = sizeof(body_acc) - body_len;
            if (data_len) {
                memcpy(body_acc + body_len, payload + data_off, data_len);
                body_len += data_len;
            }
            if (flags & 0x01)
                stream_done = 1;
        }
    }

    if (!status)
        status = 200;
    if (status_out)
        *status_out = status;

    size_t so = (size_t)snprintf(out, out_cap,
                                 "HTTP/1.0 %d OK\r\nContent-Type: text/html\r\n"
                                 "Connection: close\r\n\r\n",
                                 status);
    if (so >= out_cap)
        return -1;
    if (so + body_len >= out_cap)
        body_len = out_cap - so - 1;
    if (body_len)
        memcpy(out + so, body_acc, body_len);
    out[so + body_len] = '\0';
    return 0;
}
