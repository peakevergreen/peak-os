/*
 * Minimal HTTP/2 GET/POST client (RFC 7540 subset) for ALPN h2 sessions.
 *
 * Fixes vs earlier lite client:
 * - Accept frames up to HTTP2_MAX_FRAME (16 KiB); do not fail when plen > body cap
 * - Heap buffers for frame + body (no huge stacks)
 * - ACK PING; send WINDOW_UPDATE after DATA (flow control)
 * - Accumulate HEADERS+CONTINUATION until END_HEADERS
 */
#include "http2.h"
#include "hpack.h"
#include "tls.h"
#include "net_internal.h"
#include "timer.h"
#include "util.h"
#include "heap.h"

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

static int send_window_update(uint32_t stream, uint32_t incr) {
    uint8_t p[4];
    if (incr == 0 || incr > 0x7fffffffu)
        return 0;
    wr32(p, incr);
    return send_frame(0x08, 0, stream, p, 4);
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

/* Drain plen bytes from TLS without storing (oversized / ignored frames). */
static int recv_discard(size_t plen) {
    uint8_t tmp[256];
    while (plen) {
        size_t chunk = plen > sizeof(tmp) ? sizeof(tmp) : plen;
        if (recv_exact(tmp, chunk) != 0)
            return -1;
        plen -= chunk;
    }
    return 0;
}

static int hpack_add_lit_req(uint8_t *buf, size_t cap, size_t *o, uint8_t name_idx,
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

static int send_client_settings(void) {
    uint8_t p[24];
    size_t o = 0;
    /* ENABLE_PUSH=0 */
    p[o++] = 0x00;
    p[o++] = 0x02;
    p[o++] = 0;
    p[o++] = 0;
    p[o++] = 0;
    p[o++] = 0;
    /* MAX_CONCURRENT_STREAMS=100 */
    p[o++] = 0x00;
    p[o++] = 0x03;
    p[o++] = 0;
    p[o++] = 0;
    p[o++] = 0;
    p[o++] = 100;
    /* INITIAL_WINDOW_SIZE=256 KiB */
    p[o++] = 0x00;
    p[o++] = 0x04;
    p[o++] = 0;
    p[o++] = 0x04;
    p[o++] = 0;
    p[o++] = 0;
    /* MAX_FRAME_SIZE=16384 */
    p[o++] = 0x00;
    p[o++] = 0x05;
    p[o++] = 0;
    p[o++] = 0;
    p[o++] = 0x40;
    p[o++] = 0x00;
    return send_frame(0x04, 0, 0, p, o);
}

static const char *status_phrase(int st) {
    switch (st) {
    case 200: return "OK";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default:  return "OK";
    }
}

void http2_last_meta(struct http2_meta *out) {
    if (out)
        *out = last_meta;
}

int http2_get(const char *host, const char *path, const char *extra_headers, char *out,
              size_t out_cap, int *status_out) {
    return http2_request("GET", host, path, extra_headers, NULL, 0, out, out_cap,
                         status_out);
}

int http2_request(const char *method, const char *host, const char *path,
                  const char *extra_headers, const char *body, size_t body_len,
                  char *out, size_t out_cap, int *status_out) {
    (void)extra_headers;
    memset(&last_meta, 0, sizeof(last_meta));
    if (!host || !path || !out || out_cap < 128)
        return -1;
    if (!method)
        method = "GET";

    uint8_t *payload = (uint8_t *)kmalloc(HTTP2_MAX_FRAME);
    uint8_t *body_acc = (uint8_t *)kmalloc(HTTP2_BODY_MAX);
    uint8_t *hdr_block = (uint8_t *)kmalloc(HTTP2_MAX_FRAME * 2);
    if (!payload || !body_acc || !hdr_block) {
        kfree(payload);
        kfree(body_acc);
        kfree(hdr_block);
        return -1;
    }
    size_t hdr_block_len = 0;
    size_t hdr_block_cap = HTTP2_MAX_FRAME * 2;

    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    if (tls_send(preface, sizeof(preface) - 1) != 0)
        goto fail;

    /* Empty SETTINGS + large connection window (many peers delay DATA until window). */
    if (send_frame(0x04, 0, 0, NULL, 0) != 0)
        goto fail;
    (void)send_client_settings;
    if (send_window_update(0, 256u * 1024u) != 0)
        goto fail;

    uint8_t hpack[256];
    size_t ho = 0;
    if (!strcmp(method, "POST"))
        hpack[ho++] = 0x83;
    else
        hpack[ho++] = 0x82;
    hpack[ho++] = 0x87;
    if (!strcmp(path, "/"))
        hpack[ho++] = 0x84;
    else if (hpack_add_lit_req(hpack, sizeof(hpack), &ho, 4, path) != 0)
        goto fail;
    if (hpack_add_lit_req(hpack, sizeof(hpack), &ho, 1, host) != 0)
        goto fail;
    /* user-agent (literal name+value) — some CDNs send empty bodies to bare clients */
    if (ho + 2 + 10 + 2 + 16 < sizeof(hpack)) {
        hpack[ho++] = 0x00; /* literal without indexing, new name */
        hpack[ho++] = 10;
        memcpy(hpack + ho, "user-agent", 10);
        ho += 10;
        hpack[ho++] = 16;
        memcpy(hpack + ho, "PeakOS-wget/0.2.0", 16);
        ho += 16;
    }
    if (ho + 2 + 6 + 2 + 3 < sizeof(hpack)) {
        hpack[ho++] = 0x00;
        hpack[ho++] = 6;
        memcpy(hpack + ho, "accept", 6);
        ho += 6;
        hpack[ho++] = 3;
        memcpy(hpack + ho, "*/*", 3);
        ho += 3;
    }
    if (ho + 2 + 15 + 2 + 8 < sizeof(hpack)) {
        hpack[ho++] = 0x00;
        hpack[ho++] = 15;
        memcpy(hpack + ho, "accept-encoding", 15);
        ho += 15;
        hpack[ho++] = 8;
        memcpy(hpack + ho, "identity", 8);
        ho += 8;
    }
    if (body_len > 0) {
        char cl[16];
        snprintf(cl, sizeof(cl), "%zu", body_len);
        if (hpack_add_lit_req(hpack, sizeof(hpack), &ho, 28, cl) != 0)
            goto fail;
    }
    uint8_t hdr_flags = body_len ? 0x04 : 0x05; /* END_HEADERS [, END_STREAM] */
    if (send_frame(0x01, hdr_flags, 1, hpack, ho) != 0)
        goto fail;
    if (body_len > 0) {
        size_t send_len = body_len;
        if (send_len > HTTP2_BODY_MAX)
            send_len = HTTP2_BODY_MAX;
        /* Chunk into MAX_FRAME-sized DATA frames. */
        size_t off = 0;
        while (off < send_len) {
            size_t chunk = send_len - off;
            if (chunk > HTTP2_MAX_FRAME)
                chunk = HTTP2_MAX_FRAME;
            uint8_t df = (off + chunk >= send_len) ? 0x01 : 0x00; /* END_STREAM on last */
            if (send_frame(0x00, df, 1, (const uint8_t *)body + off, chunk) != 0)
                goto fail;
            off += chunk;
        }
    }

    int status = 0;
    char resp_hdrs[2048];
    size_t resp_hdr_off = 0;
    size_t body_stored = 0, body_total = 0;
    int stream_done = 0;
    int headers_done = 0;
    int saw_end_stream = 0;
    uint64_t last_progress = timer_ticks();

    while (!stream_done && !net_timed_out(last_progress, NET_HTTP_IDLE_TLS_TICKS)) {
        uint8_t fh[9];
        if (recv_exact(fh, 9) != 0)
            break;
        uint32_t plen = rd24(fh);
        uint8_t type = fh[3], flags = fh[4];
        uint32_t sid = rd32(fh + 5) & 0x7fffffffu;

        if (plen > HTTP2_MAX_FRAME) {
            /* Protocol violation or unsupported large frame — drain and fail. */
            if (recv_discard(plen) != 0)
                goto fail;
            goto fail;
        }

        if (plen) {
            if (recv_exact(payload, plen) != 0)
                goto fail;
        }

        last_progress = timer_ticks();
        last_meta.frames_in++;
        if (last_meta.ntrace < HTTP2_TRACE_MAX) {
            struct http2_frame_trace *tr = &last_meta.trace[last_meta.ntrace++];
            tr->type = type;
            tr->flags = flags;
            tr->sid = sid;
            tr->plen = plen;
        }
        if (type == 0x01 && payload && plen && !last_meta.first_hpack[0]) {
            size_t n = plen > 4 ? 4 : plen;
            memcpy(last_meta.first_hpack, payload, n);
        }

        if (type == 0x04) { /* SETTINGS */
            if (!(flags & 0x01) && send_frame(0x04, 0x01, 0, NULL, 0) != 0)
                goto fail;
            continue;
        }
        if (type == 0x06) { /* PING — must ACK */
            if (!(flags & 0x01) && plen == 8) {
                if (send_frame(0x06, 0x01, 0, payload, 8) != 0)
                    goto fail;
            }
            continue;
        }
        if (type == 0x08) /* WINDOW_UPDATE from peer */
            continue;
        if (type == 0x07) { /* GOAWAY */
            last_meta.goaway = 1;
            break;
        }
        if (type == 0x03) { /* RST_STREAM */
            if (sid == 1) {
                last_meta.rst = 1;
                stream_done = 1;
            }
            continue;
        }

        if (sid != 1 && type != 0x09)
            continue;

        if (type == 0x01 || type == 0x09) { /* HEADERS or CONTINUATION */
            size_t off = 0;
            if (type == 0x01) {
                hdr_block_len = 0;
                if (flags & 0x08) { /* PADDED */
                    if (!plen)
                        continue;
                    off = 1 + payload[0];
                }
                if (flags & 0x20) /* PRIORITY */
                    off += 5;
                if (flags & 0x01) {
                    saw_end_stream = 1;
                    last_meta.headers_end_stream = 1;
                }
            }
            if (off > plen)
                continue;
            size_t frag = plen - off;
            if (hdr_block_len + frag > hdr_block_cap)
                frag = hdr_block_cap - hdr_block_len;
            if (frag) {
                memcpy(hdr_block + hdr_block_len, payload + off, frag);
                hdr_block_len += frag;
            }
            if (flags & 0x04) { /* END_HEADERS */
                if (hpack_decode_block(hdr_block, hdr_block_len, &status, resp_hdrs,
                                       sizeof(resp_hdrs), &resp_hdr_off) != 0)
                    goto fail;
                if (!status)
                    status = 200;
                headers_done = 1;
                hdr_block_len = 0;
                /* END_STREAM on HEADERS is definitive (no DATA). 204/304 often empty. */
                if (saw_end_stream)
                    stream_done = 1;
            }
            continue;
        }

        if (type == 0x00) { /* DATA */
            size_t data_off = 0, data_len = plen;
            int end = (flags & 0x01) ? 1 : 0;
            if (flags & 0x08) {
                if (plen >= 1) {
                    uint8_t pad = payload[0];
                    data_off = 1;
                    if (1 + pad <= plen)
                        data_len = plen - 1 - pad;
                    else
                        data_len = 0;
                } else {
                    data_len = 0;
                }
            }
            body_total += data_len;
            size_t copy = data_len;
            if (body_stored + copy > HTTP2_BODY_MAX)
                copy = HTTP2_BODY_MAX - body_stored;
            if (copy) {
                memcpy(body_acc + body_stored, payload + data_off, copy);
                body_stored += copy;
            }
            /* Replenish flow-control windows for connection + stream. */
            if (plen && send_window_update(0, plen) != 0)
                goto fail;
            if (plen && send_window_update(1, plen) != 0)
                goto fail;
            if (end)
                stream_done = 1;
            continue;
        }
    }

    if (!status) {
        /* Unseen :status after a partial/empty stream: do not invent 200 when
         * headers were decoded. Timeout with zero HEADERS still reports 0. */
        if (!headers_done && !last_meta.rst && !last_meta.goaway && last_meta.frames_in)
            status = 0;
    }
    if (status_out)
        *status_out = status;

    last_meta.status = status;
    last_meta.body_stored = body_stored;
    last_meta.body_total = body_total;
    last_meta.truncated = (body_total > body_stored) ? 1 : 0;

    size_t so = (size_t)snprintf(out, out_cap, "HTTP/1.0 %d %s\r\n", status,
                                 status_phrase(status));
    if (so >= out_cap)
        goto fail;
    if (resp_hdr_off) {
        if (so + resp_hdr_off >= out_cap)
            goto fail;
        memcpy(out + so, resp_hdrs, resp_hdr_off);
        so += resp_hdr_off;
    } else {
        int n = snprintf(out + so, out_cap - so, "Content-Type: text/html\r\n");
        if (n < 0 || (size_t)n >= out_cap - so)
            goto fail;
        so += (size_t)n;
    }
    {
        int n = snprintf(out + so, out_cap - so,
                         "Connection: close\r\n"
                         "X-Peak-HTTP2: 1\r\n");
        if (n < 0 || (size_t)n >= out_cap - so)
            goto fail;
        so += (size_t)n;
        if (last_meta.truncated) {
            n = snprintf(out + so, out_cap - so,
                         "X-Peak-Truncated: 1\r\n"
                         "X-Peak-Body-Total: %zu\r\n",
                         body_total);
            if (n > 0 && (size_t)n < out_cap - so)
                so += (size_t)n;
        }
        n = snprintf(out + so, out_cap - so, "\r\n");
        if (n < 0 || (size_t)n >= out_cap - so)
            goto fail;
        so += (size_t)n;
    }
    if (so + body_stored >= out_cap) {
        body_stored = out_cap - so - 1;
        last_meta.truncated = 1;
        last_meta.body_stored = body_stored;
    }
    if (body_stored)
        memcpy(out + so, body_acc, body_stored);
    out[so + body_stored] = '\0';
    last_meta.body_stored = body_stored;
    last_meta.message_len = so + body_stored;

    (void)headers_done;
    kfree(payload);
    kfree(body_acc);
    kfree(hdr_block);
    return 0;

fail:
    kfree(payload);
    kfree(body_acc);
    kfree(hdr_block);
    return -1;
}
