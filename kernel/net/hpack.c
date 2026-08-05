/*
 * HPACK (RFC 7541) — static table + Huffman + integer coding for Peak HTTP/2.
 * Dynamic table is not stored; SETTINGS HEADER_TABLE_SIZE=0 asks peers not to use it.
 */
#ifdef PEAK_HOST_TEST
#include "../include/hpack.h"
#include <string.h>
#include <stdio.h>
#else
#include "hpack.h"
#include "util.h"
#endif

/* RFC 7541 Appendix B */
static const uint32_t huff_code[256] = {
    0x1ff8,     0x7fffd8,   0xfffffe2,  0xfffffe3,  0xfffffe4,  0xfffffe5,  0xfffffe6,  0xfffffe7,
    0xfffffe8,  0xffffea,   0x3ffffffc, 0xfffffe9,  0xfffffea,  0x3ffffffd, 0xfffffeb,  0xfffffec,
    0xfffffed,  0xfffffee,  0xfffffef,  0xffffff0,  0xffffff1,  0xffffff2,  0x3ffffffe, 0xffffff3,
    0xffffff4,  0xffffff5,  0xffffff6,  0xffffff7,  0xffffff8,  0xffffff9,  0xffffffa,  0xffffffb,
    0x14,       0x3f8,      0x3f9,      0xffa,      0x1ff9,     0x15,       0xf8,       0x7fa,
    0x3fa,      0x3fb,      0xf9,       0x7fb,      0xfa,       0x16,       0x17,       0x18,
    0x0,        0x1,        0x2,        0x19,       0x1a,       0x1b,       0x1c,       0x1d,
    0x1e,       0x1f,       0x5c,       0xfb,       0x7ffc,     0x20,       0xffb,      0x3fc,
    0x1ffa,     0x21,       0x5d,       0x5e,       0x5f,       0x60,       0x61,       0x62,
    0x63,       0x64,       0x65,       0x66,       0x67,       0x68,       0x69,       0x6a,
    0x6b,       0x6c,       0x6d,       0x6e,       0x6f,       0x70,       0x71,       0x72,
    0xfc,       0x73,       0xfd,       0x1ffb,     0x7fff0,    0x1ffc,     0x3ffc,     0x22,
    0x7ffd,     0x3,        0x23,       0x4,        0x24,       0x5,        0x25,       0x26,
    0x27,       0x6,        0x74,       0x75,       0x28,       0x29,       0x2a,       0x7,
    0x2b,       0x76,       0x2c,       0x8,        0x9,        0x2d,       0x77,       0x78,
    0x79,       0x7a,       0x7b,       0x7ffe,     0x7fc,      0x3ffd,     0x1ffd,     0xffffffc,
    0xfffe6,    0x3fffd2,   0xfffe7,    0xfffe8,    0x3fffd3,   0x3fffd4,   0x3fffd5,   0x7fffd9,
    0x3fffd6,   0x7fffda,   0x7fffdb,   0x7fffdc,   0x7fffdd,   0x7fffde,   0xffffeb,   0x7fffdf,
    0xffffec,   0xffffed,   0x3fffd7,   0x7fffe0,   0xffffee,   0x7fffe1,   0x7fffe2,   0x7fffe3,
    0x7fffe4,   0x1fffdc,   0x3fffd8,   0x7fffe5,   0x3fffd9,   0x7fffe6,   0x7fffe7,   0xffffef,
    0x3fffda,   0x1fffdd,   0xfffe9,    0x3fffdb,   0x3fffdc,   0x7fffe8,   0x7fffe9,   0x1fffde,
    0x7fffea,   0x3fffdd,   0x3fffde,   0xfffff0,   0x1fffdf,   0x3fffdf,   0x7fffeb,   0x7fffec,
    0x1fffe0,   0x1fffe1,   0x3fffe0,   0x1fffe2,   0x7fffed,   0x3fffe1,   0x7fffee,   0x7fffef,
    0xfffea,    0x3fffe2,   0x3fffe3,   0x3fffe4,   0x7ffff0,   0x3fffe5,   0x3fffe6,   0x7ffff1,
    0x3ffffe0,  0x3ffffe1,  0xfffeb,    0x7fff1,    0x3fffe7,   0x7ffff2,   0x3fffe8,   0x1ffffec,
    0x3ffffe2,  0x3ffffe3,  0x3ffffe4,  0x7ffffde,  0x7ffffdf,  0x3ffffe5,  0xfffff1,   0x1ffffed,
    0x7fff2,    0x1fffe3,   0x3ffffe6,  0x7ffffe0,  0x7ffffe1,  0x3ffffe7,  0x7ffffe2,  0xfffff2,
    0x1fffe4,   0x1fffe5,   0x3ffffe8,  0x3ffffe9,  0xffffffd,  0x7ffffe3,  0x7ffffe4,  0x7ffffe5,
    0xfffec,    0xfffff3,   0xfffed,    0x1fffe6,   0x3fffe9,   0x1fffe7,   0x1fffe8,   0x7ffff3,
    0x3fffea,   0x3fffeb,   0x1ffffee,  0x1ffffef,  0xfffff4,   0xfffff5,   0x3ffffea,  0x7ffff4,
    0x3ffffeb,  0x7ffffe6,  0x3ffffec,  0x3ffffed,  0x7ffffe7,  0x7ffffe8,  0x7ffffe9,  0x7ffffea,
    0x7ffffeb,  0xffffffe,  0x7ffffec,  0x7ffffed,  0x7ffffee,  0x7ffffef,  0x7fffff0,  0x3ffffee,
};

static const uint8_t huff_len[256] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 30, 28,
    28, 28, 28, 28, 28, 28, 28, 28, 6,  10, 10, 12, 13, 6,  8,  11, 10, 10, 8,  11, 8,  6,  6,  6,
    5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  7,  8,  15, 6,  12, 10, 13, 6,  7,  7,  7,  7,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  8,  7,  8,  13, 19, 13, 14, 6,
    15, 5,  6,  5,  6,  5,  6,  6,  6,  5,  7,  7,  6,  6,  6,  5,  6,  7,  6,  5,  5,  6,  7,  7,
    7,  7,  7,  15, 11, 14, 13, 28, 20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23, 23, 24, 23,
    24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24, 22, 21, 20, 22, 22, 23, 23, 21,
    23, 22, 22, 24, 21, 22, 23, 23, 21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23,
    26, 26, 20, 19, 22, 23, 22, 25, 26, 26, 26, 27, 27, 26, 24, 25, 19, 21, 26, 27, 27, 26, 27, 24,
    21, 21, 26, 26, 28, 27, 27, 27, 20, 24, 20, 21, 22, 21, 21, 23, 22, 22, 25, 25, 24, 24, 26, 23,
    26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26,
};

/* RFC 7541 Appendix A — names (index 1..61). */
static const char *const st_name[62] = {
    NULL,
    ":authority",
    ":method",
    ":method",
    ":path",
    ":path",
    ":scheme",
    ":scheme",
    ":status",
    ":status",
    ":status",
    ":status",
    ":status",
    ":status",
    ":status",
    "accept-charset",
    "accept-encoding",
    "accept-language",
    "accept-ranges",
    "accept",
    "access-control-allow-origin",
    "age",
    "allow",
    "authorization",
    "cache-control",
    "content-disposition",
    "content-encoding",
    "content-language",
    "content-length",
    "content-location",
    "content-range",
    "content-type",
    "cookie",
    "date",
    "etag",
    "expect",
    "expires",
    "from",
    "host",
    "if-match",
    "if-modified-since",
    "if-none-match",
    "if-range",
    "if-unmodified-since",
    "last-modified",
    "link",
    "location",
    "max-forwards",
    "proxy-authenticate",
    "proxy-authorization",
    "range",
    "referer",
    "refresh",
    "retry-after",
    "server",
    "set-cookie",
    "strict-transport-security",
    "transfer-encoding",
    "user-agent",
    "vary",
    "via",
    "www-authenticate",
};

static const char *const st_val[62] = {
    NULL, "", "GET", "POST", "/", "/index.html", "http", "https", "200", "204", "206", "304", "400",
    "404", "500", "", "gzip, deflate", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "",
};

static void cpyz(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    if (!dst || !cap)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int parse_dec(const char *p) {
    int v = 0;
    if (!p)
        return 0;
    for (; *p >= '0' && *p <= '9'; p++)
        v = v * 10 + (*p - '0');
    return v;
}

int hpack_huff_decode(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
    uint64_t acc = 0;
    int bits = 0;
    size_t o = 0, i = 0;
    if (!out || out_cap < 1)
        return -1;
    out[0] = '\0';
    if (!in && in_len)
        return -1;
    while (i < in_len || bits > 0) {
        while (bits < 30 && i < in_len) {
            acc = (acc << 8) | in[i++];
            bits += 8;
        }
        int matched = 0;
        for (int sym = 0; sym < 256; sym++) {
            int n = huff_len[sym];
            uint32_t mask;
            uint32_t got;
            if (bits < n)
                continue;
            mask = (n >= 32) ? 0xffffffffu : ((1u << n) - 1u);
            got = (uint32_t)(acc >> (bits - n)) & mask;
            if (got != huff_code[sym])
                continue;
            if (o + 1 >= out_cap)
                return -1;
            out[o++] = (char)(unsigned char)sym;
            bits -= n;
            if (bits == 0)
                acc = 0;
            else
                acc &= ((uint64_t)1 << bits) - 1;
            matched = 1;
            break;
        }
        if (matched)
            continue;
        if (i >= in_len) {
            if (bits > 0 && bits <= 7) {
                uint32_t pad = (uint32_t)acc & (((uint32_t)1 << bits) - 1u);
                uint32_t ones = ((uint32_t)1 << bits) - 1u;
                if (pad == ones) {
                    out[o] = '\0';
                    return 0;
                }
            }
            if (bits == 0) {
                out[o] = '\0';
                return 0;
            }
            return -1;
        }
        if (bits >= 30)
            return -1;
    }
    out[o] = '\0';
    return 0;
}

static int hpack_dec_int(const uint8_t *p, size_t len, size_t *i, unsigned nbits, uint32_t *out) {
    uint32_t max, v, m;
    if (!p || !i || !out || *i >= len || nbits == 0 || nbits > 8)
        return -1;
    max = (1u << nbits) - 1u;
    v = p[*i] & max;
    (*i)++;
    if (v < max) {
        *out = v;
        return 0;
    }
    m = 0;
    while (*i < len) {
        uint8_t b = p[(*i)++];
        if (m > 28)
            return -1;
        v += (uint32_t)(b & 0x7f) << m;
        m += 7;
        if (!(b & 0x80)) {
            *out = v;
            return 0;
        }
    }
    return -1;
}

int hpack_enc_int(uint8_t *buf, size_t cap, size_t *o, uint32_t v, unsigned nbits,
                  uint8_t prefix_hi) {
    uint32_t max;
    if (!buf || !o || nbits == 0 || nbits > 8 || *o >= cap)
        return -1;
    max = (1u << nbits) - 1u;
    if (v < max) {
        buf[(*o)++] = (uint8_t)(prefix_hi | v);
        return 0;
    }
    buf[(*o)++] = (uint8_t)(prefix_hi | max);
    v -= max;
    while (v >= 128) {
        if (*o >= cap)
            return -1;
        buf[(*o)++] = (uint8_t)((v & 0x7f) | 0x80);
        v >>= 7;
    }
    if (*o >= cap)
        return -1;
    buf[(*o)++] = (uint8_t)v;
    return 0;
}

int hpack_add_lit(uint8_t *buf, size_t cap, size_t *o, uint32_t name_idx, const char *val) {
    size_t vlen;
    if (!val)
        return -1;
    /* Literal without indexing, indexed name (prefix 4). */
    if (hpack_enc_int(buf, cap, o, name_idx, 4, 0x00) != 0)
        return -1;
    vlen = strlen(val);
    if (hpack_enc_int(buf, cap, o, (uint32_t)vlen, 7, 0x00) != 0)
        return -1;
    if (*o + vlen > cap)
        return -1;
    memcpy(buf + *o, val, vlen);
    *o += vlen;
    return 0;
}

static int hdr_append(char *buf, size_t cap, size_t *off, const char *name, const char *val) {
    int n;
    if (!name || !val || !name[0] || name[0] == ':')
        return 0;
    n = snprintf(buf + *off, cap - *off, "%s: %s\r\n", name, val);
    if (n < 0 || (size_t)n >= cap - *off)
        return -1;
    *off += (size_t)n;
    return 0;
}

static int hpack_emit(char *hdr_buf, size_t hdr_cap, size_t *hdr_off, const char *name,
                      const char *val, int *status_out) {
    if (!name)
        return 0;
    if (name[0] == ':' && name[1] == 's' && !strncmp(name, ":status", 7)) {
        if (status_out && val && val[0])
            *status_out = parse_dec(val);
        return 0;
    }
    return hdr_append(hdr_buf, hdr_cap, hdr_off, name, val ? val : "");
}

static int hpack_read_str(const uint8_t *p, size_t len, size_t *i, char *out, size_t out_cap) {
    int huff;
    uint32_t slen = 0;
    if (*i >= len || !out || out_cap < 1)
        return -1;
    huff = p[*i] & 0x80;
    if (hpack_dec_int(p, len, i, 7, &slen) != 0)
        return -1;
    if (*i + slen > len)
        return -1;
    if (huff) {
        if (hpack_huff_decode(p + *i, slen, out, out_cap) != 0)
            return -1;
    } else {
        if (slen >= out_cap)
            return -1;
        memcpy(out, p + *i, slen);
        out[slen] = '\0';
    }
    *i += slen;
    return 0;
}

static const char *static_name(uint32_t idx) {
    return (idx >= 1 && idx <= 61) ? st_name[idx] : NULL;
}

static const char *static_val(uint32_t idx) {
    return (idx >= 1 && idx <= 61) ? st_val[idx] : NULL;
}

int hpack_decode_block(const uint8_t *p, size_t len, int *status_out, char *hdr_buf,
                       size_t hdr_cap, size_t *hdr_off) {
    size_t i = 0;
    if (!p)
        return -1;
    while (i < len) {
        uint8_t b = p[i];
        if (b & 0x80) {
            uint32_t idx = 0;
            if (hpack_dec_int(p, len, &i, 7, &idx) != 0)
                break;
            (void)hpack_emit(hdr_buf, hdr_cap, hdr_off, static_name(idx), static_val(idx),
                             status_out);
            continue;
        }
        if ((b & 0xe0) == 0x20) {
            uint32_t sz = 0;
            if (hpack_dec_int(p, len, &i, 5, &sz) != 0)
                break;
            continue;
        }
        if ((b & 0xc0) == 0x40 || (b & 0xf0) == 0x00 || (b & 0xf0) == 0x10) {
            uint32_t nidx = 0;
            unsigned nbits = ((b & 0xc0) == 0x40) ? 6 : 4;
            char name[96], val[384];
            name[0] = val[0] = '\0';
            if (hpack_dec_int(p, len, &i, nbits, &nidx) != 0)
                break;
            if (nidx == 0) {
                if (hpack_read_str(p, len, &i, name, sizeof(name)) != 0)
                    break;
            } else {
                const char *sn = static_name(nidx);
                if (sn)
                    cpyz(name, sizeof(name), sn);
                /* Dynamic-table name index: still consume value to stay in sync. */
            }
            if (hpack_read_str(p, len, &i, val, sizeof(val)) != 0)
                break;
            if (name[0])
                (void)hpack_emit(hdr_buf, hdr_cap, hdr_off, name, val, status_out);
            continue;
        }
        break;
    }
    return 0;
}
