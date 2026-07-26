/*
 * Minimal HTTP/2 GET client (RFC 7540 subset) for ALPN h2 sessions.
 */
#include "http2.h"
#include "tls.h"
#include "net_internal.h"
#include "timer.h"
#include "util.h"

static struct http2_meta last_meta;

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
    buf[(*o)++] = (uint8_t)(name_idx & 0x0f);
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

static const char *status_phrase(int st) {
    switch (st) {
    case 200: return "OK";
    case 204: return "No Content";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default:  return "OK";
    }
}

static int hpack_static_entry(uint8_t idx, const char **name_out, const char **val_out) {
    static const struct { uint8_t idx; const char *name; const char *val; } tbl[] = {
        {8, ":status", "200"}, {9, ":status", "204"}, {10, ":status", "206"},
        {11, ":status", "304"}, {12, ":status", "400"}, {13, ":status", "404"},
        {14, ":status", "500"}, {17, "cache-control", "no-cache"},
        {22, "content-type", "text/html"}, {24, "content-type", "application/octet-stream"},
        {25, "content-type", "application/json"},
        {31, "content-type", "text/html; charset=utf-8"},
        {44, "location", ""}, {46, "location", ""}, {54, "server", ""},
    };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        if (tbl[i].idx == idx) {
            if (name_out) *name_out = tbl[i].name;
            if (val_out) *val_out = tbl[i].val;
            return 0;
        }
    }
    return -1;
}

static const char *hpack_static_name(uint8_t idx) {
    const char *n = NULL, *v = NULL;
    return hpack_static_entry(idx, &n, &v) == 0 ? n : NULL;
}

static int hdr_append(char *buf, size_t cap, size_t *off, const char *name, const char *val) {
    if (!name || !val || !name[0] || name[0] == ':')
        return 0;
    int n = snprintf(buf + *off, cap - *off, "%s: %s\r\n", name, val);
    if (n < 0 || (size_t)n >= cap - *off)
        return -1;
    *off += (size_t)n;
    return 0;
}

static int hpack_emit_field(char *hdr_buf, size_t hdr_cap, size_t *hdr_off, const char *name,
                            const char *val, int *status_out) {
    if (!name)
        return 0;
    if (!strncmp(name, ":status", 7)) {
        if (status_out && val && val[0])
            *status_out = parse_dec((const uint8_t *)val, strlen(val));
        return 0;
    }
    return hdr_append(hdr_buf, hdr_cap, hdr_off, name, val ? val : "");
}

static int hpack_read_str(const uint8_t *p, size_t len, size_t *i, char *out, size_t out_cap) {
    if (*i >= len)
        return -1;
    size_t slen = p[(*i)++] & 0x7f;
    if (*i + slen > len)
        return -1;
    if (slen >= out_cap)
        slen = out_cap - 1;
    memcpy(out, p + *i, slen);
    out[slen] = '\0';
    *i += slen;
    return 0;
}

static int hpack_decode_block(const uint8_t *p, size_t len, int *status_out, char *hdr_buf,
                              size_t hdr_cap, size_t *hdr_off) {
    size_t i = 0;
    while (i < len) {
        uint8_t b = p[i++];
        if (b & 0x80) {
            const char *name = NULL, *val = NULL;
            if (hpack_static_entry(b & 0x7f, &name, &val) == 0 &&
                hpack_emit_field(hdr_buf, hdr_cap, hdr_off, name, val, status_out) != 0)
                return -1;
            continue;
        }
        if ((b & 0xf0) == 0x00) {
            char name[96], val[256];
            name[0] = val[0] = '\0';
            uint8_t nidx = b & 0x0f;
            if (nidx == 0) {
                if (hpack_read_str(p, len, &i, name, sizeof(name)) != 0)
                    break;
            } else {
                const char *sn = hpack_static_name(nidx);
                if (!sn)
                    break;
                snprintf(name, sizeof(name), "%s", sn);
            }
            if (hpack_read_str(p, len, &i, val, sizeof(val)) != 0)
                break;
            if (hpack_emit_field(hdr_buf, hdr_cap, hdr_off, name, val, status_out) != 0)
                return -1;
            continue;
        }
        if ((b & 0xc0) == 0x40) {
            if ((b & 0x3f) == 0) {
                char skip[96];
                if (hpack_read_str(p, len, &i, skip, sizeof(skip)) != 0)
                    break;
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

void http2_last_meta(struct http2_meta *out) {
    if (out)
        *out = last_meta;
}

int http2_get(const char *host, const char *path, const char *extra_headers, char *out,
              size_t out_cap, int *status_out) {
    (void)extra_headers;
    memset(&last_meta, 0, sizeof(last_meta));
    if (!host || !path || !out || out_cap < 128)
        return -1;

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (tls_send(preface, sizeof(preface) - 1) != 0)
        return -1;
    if (send_frame(0x04, 0, 0, NULL, 0) != 0)
        return -1;

    uint8_t hpack[256];
    size_t ho = 0;
    hpack[ho++] = 0x82;
    hpack[ho++] = 0x87;
    if (!strcmp(path, "/"))
        hpack[ho++] = 0x84;
    else if (hpack_add_lit(hpack, sizeof(hpack), &ho, 4, path) != 0)
        return -1;
    if (hpack_add_lit(hpack, sizeof(hpack), &ho, 1, host) != 0)
        return -1;
    if (send_frame(0x01, 0x05, 1, hpack, ho) != 0)
        return -1;

    int status = 0;
    char resp_hdrs[2048];
    size_t resp_hdr_off = 0;
    uint8_t body_acc[HTTP2_BODY_MAX];
    size_t body_len = 0, body_total = 0;
    int stream_done = 0;
    uint64_t start = timer_ticks();

    while (!stream_done && !net_timed_out(start, NET_HTTP_IDLE_TLS_TICKS)) {
        uint8_t fh[9];
        if (recv_exact(fh, 9) != 0)
            break;
        uint32_t plen = rd24(fh);
        uint8_t type = fh[3], flags = fh[4];
        uint32_t sid = rd32(fh + 5) & 0x7fffffffu;
        if (plen > HTTP2_BODY_MAX)
            return -1;
        uint8_t payload[HTTP2_BODY_MAX];
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
            if (off < plen &&
                hpack_decode_block(payload + off, plen - off, &status, resp_hdrs,
                                   sizeof(resp_hdrs), &resp_hdr_off) != 0)
                return -1;
            if (!status)
                status = 200;
            if (flags & 0x01)
                stream_done = 1;
            continue;
        }
        if (type == 0x00) {
            size_t data_off = 0, data_len = plen;
            if (flags & 0x08) {
                if (!plen)
                    continue;
                uint8_t pad = payload[0];
                data_off = 1;
                if (1 + pad > plen)
                    continue;
                data_len = plen - 1 - pad;
            }
            body_total += data_len;
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

    last_meta.status = status;
    last_meta.body_stored = body_len;
    last_meta.body_total = body_total;
    last_meta.truncated = (body_total > body_len) ? 1 : 0;

    size_t so = (size_t)snprintf(out, out_cap, "HTTP/1.0 %d %s\r\n", status,
                                 status_phrase(status));
    if (so >= out_cap)
        return -1;
    if (resp_hdr_off) {
        if (so + resp_hdr_off >= out_cap)
            return -1;
        memcpy(out + so, resp_hdrs, resp_hdr_off);
        so += resp_hdr_off;
    } else {
        int n = snprintf(out + so, out_cap - so, "Content-Type: text/html\r\n");
        if (n < 0 || (size_t)n >= out_cap - so)
            return -1;
        so += (size_t)n;
    }
    {
        int n = snprintf(out + so, out_cap - so, "Connection: close\r\n"
                                                   "X-Peak-HTTP2: 1\r\n\r\n");
        if (n < 0 || (size_t)n >= out_cap - so)
            return -1;
        so += (size_t)n;
    }
    if (so + body_len >= out_cap) {
        body_len = out_cap - so - 1;
        last_meta.truncated = 1;
        last_meta.body_stored = body_len;
    }
    if (body_len)
        memcpy(out + so, body_acc, body_len);
    out[so + body_len] = '\0';
    return 0;
}
