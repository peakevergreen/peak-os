#include "inflate_lite.h"
#include "heap.h"
#include "util.h"

#define INFL_MAX_SYMS 288

typedef struct {
    const uint8_t *in;
    size_t in_len;
    size_t byte_pos;
    int bit_pos;
} infl_bits;

typedef struct {
    uint16_t sym;
    uint16_t bits;
} infl_code;

typedef struct {
    infl_code *codes;
    int ncodes;
    int maxbits;
} infl_huff;

static int infl_read_bits(infl_bits *b, int n, uint32_t *out) {
    uint32_t v = 0;
    int i;
    if (n < 0 || n > 16)
        return -1;
    for (i = 0; i < n; i++) {
        if (b->byte_pos >= b->in_len)
            return -1;
        v |= (uint32_t)((b->in[b->byte_pos] >> b->bit_pos) & 1) << i;
        b->bit_pos++;
        if (b->bit_pos == 8) {
            b->bit_pos = 0;
            b->byte_pos++;
        }
    }
    *out = v;
    return 0;
}

static void infl_align_byte(infl_bits *b) {
    if (b->bit_pos) {
        b->bit_pos = 0;
        b->byte_pos++;
    }
}

static uint32_t infl_bit_reverse(uint32_t v, int n) {
    uint32_t r = 0;
    int i;
    for (i = 0; i < n; i++)
        r |= ((v >> i) & 1u) << (n - 1 - i);
    return r;
}

static int infl_huff_build(const uint8_t *lens, int n, infl_huff *h) {
    uint16_t bl_count[16];
    uint16_t next_code[16];
    int i, bits, code, sym;

    memset(bl_count, 0, sizeof(bl_count));
    h->maxbits = 0;
    for (i = 0; i < n; i++) {
        if (lens[i] > 15)
            return -1;
        if (lens[i] > h->maxbits)
            h->maxbits = lens[i];
        if (lens[i])
            bl_count[lens[i]]++;
    }
    if (h->maxbits == 0)
        return -1;

    code = 0;
    bl_count[0] = 0;
    for (bits = 1; bits <= 15; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = (uint16_t)code;
    }

    h->ncodes = 1 << h->maxbits;
    h->codes = (infl_code *)kmalloc((size_t)h->ncodes * sizeof(infl_code));
    if (!h->codes)
        return -1;
    for (i = 0; i < h->ncodes; i++)
        h->codes[i].sym = 0xffff;

    for (bits = h->maxbits; bits >= 1; bits--) {
        for (sym = 0; sym < n; sym++) {
            if (lens[sym] != (uint8_t)bits)
                continue;
            code = next_code[bits]++;
            {
                int step = 1 << bits;
                int rev = (int)infl_bit_reverse((uint32_t)code, bits);
                for (i = rev; i < h->ncodes; i += step) {
                    h->codes[i].sym = (uint16_t)sym;
                    h->codes[i].bits = (uint16_t)bits;
                }
            }
        }
    }
    return 0;
}

static void infl_huff_free(infl_huff *h) {
    if (h->codes) {
        kfree(h->codes);
        h->codes = NULL;
    }
    h->ncodes = 0;
    h->maxbits = 0;
}

static int infl_peek_bits(infl_bits *b, int n, uint32_t *out) {
    size_t save_byte = b->byte_pos;
    int save_bit = b->bit_pos;
    int r = infl_read_bits(b, n, out);
    b->byte_pos = save_byte;
    b->bit_pos = save_bit;
    return r;
}

static int infl_huff_decode(infl_bits *b, infl_huff *h, uint16_t *sym_out) {
    uint32_t idx;
    infl_code c;

    if (h->maxbits <= 0 || !h->codes)
        return -1;
    if (infl_peek_bits(b, h->maxbits, &idx) != 0)
        return -1;
    idx &= (1u << h->maxbits) - 1u;
    c = h->codes[idx];
    if (c.sym == 0xffff || c.bits == 0)
        return -1;
    if (infl_read_bits(b, c.bits, &idx) != 0)
        return -1;
    *sym_out = c.sym;
    return 0;
}

static const uint16_t infl_len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t infl_len_bits[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t infl_dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t infl_dist_bits[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static const uint8_t infl_cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int infl_decode_block(infl_bits *b, infl_huff *lit, infl_huff *dist,
                             uint8_t *dst, size_t dst_cap, size_t *out_pos) {
    for (;;) {
        uint16_t sym;
        if (infl_huff_decode(b, lit, &sym) != 0)
            return -1;
        if (sym < 256) {
            if (*out_pos >= dst_cap)
                return -1;
            dst[(*out_pos)++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else if (sym > 285) {
            return -1;
        } else {
            uint32_t eb;
            int li = (int)sym - 257;
            uint32_t length = infl_len_base[li];
            if (infl_len_bits[li]) {
                if (infl_read_bits(b, infl_len_bits[li], &eb) != 0)
                    return -1;
                length += eb;
            }
            if (infl_huff_decode(b, dist, &sym) != 0)
                return -1;
            if (sym > 29)
                return -1;
            {
                uint32_t distance = infl_dist_base[sym];
                if (infl_dist_bits[sym]) {
                    if (infl_read_bits(b, infl_dist_bits[sym], &eb) != 0)
                        return -1;
                    distance += eb;
                }
                if (distance == 0 || distance > *out_pos)
                    return -1;
                if (*out_pos + length > dst_cap)
                    return -1;
                {
                    size_t i;
                    for (i = 0; i < length; i++)
                        dst[*out_pos] = dst[*out_pos - distance], (*out_pos)++;
                }
            }
        }
    }
}

static int infl_fixed_tables(infl_huff *lit, infl_huff *dist) {
    uint8_t lens[288 + 32];
    int i;

    for (i = 0; i <= 143; i++)
        lens[i] = 8;
    for (; i <= 255; i++)
        lens[i] = 9;
    for (; i <= 279; i++)
        lens[i] = 7;
    for (; i <= 287; i++)
        lens[i] = 8;
    if (infl_huff_build(lens, 288, lit) != 0)
        return -1;
    for (i = 0; i < 32; i++)
        lens[i] = 5;
    if (infl_huff_build(lens, 32, dist) != 0) {
        infl_huff_free(lit);
        return -1;
    }
    return 0;
}

static int infl_dynamic_tables(infl_bits *b, infl_huff *lit, infl_huff *dist) {
    uint32_t hlit, hdist, hclen;
    uint8_t cl_lens[19];
    uint8_t all_lens[288 + 32];
    infl_huff cl_huff;
    int i, n;

    memset(&cl_huff, 0, sizeof(cl_huff));
    if (infl_read_bits(b, 5, &hlit) != 0)
        return -1;
    if (infl_read_bits(b, 5, &hdist) != 0)
        return -1;
    if (infl_read_bits(b, 4, &hclen) != 0)
        return -1;
    hlit += 257;
    hdist += 1;
    hclen += 4;
    if (hlit > 286 || hdist > 32 || hclen > 19)
        return -1;

    memset(cl_lens, 0, sizeof(cl_lens));
    for (i = 0; i < (int)hclen; i++) {
        uint32_t v;
        if (infl_read_bits(b, 3, &v) != 0)
            return -1;
        cl_lens[infl_cl_order[i]] = (uint8_t)v;
    }
    if (infl_huff_build(cl_lens, 19, &cl_huff) != 0)
        return -1;

    n = 0;
    while (n < (int)(hlit + hdist)) {
        uint16_t sym;
        if (infl_huff_decode(b, &cl_huff, &sym) != 0) {
            infl_huff_free(&cl_huff);
            return -1;
        }
        if (sym < 16) {
            all_lens[n++] = (uint8_t)sym;
        } else if (sym == 16) {
            uint32_t rep, v;
            if (n == 0) {
                infl_huff_free(&cl_huff);
                return -1;
            }
            if (infl_read_bits(b, 2, &rep) != 0) {
                infl_huff_free(&cl_huff);
                return -1;
            }
            rep += 3;
            v = all_lens[n - 1];
            while (rep-- && n < (int)(hlit + hdist))
                all_lens[n++] = (uint8_t)v;
        } else if (sym == 17) {
            uint32_t rep;
            if (infl_read_bits(b, 3, &rep) != 0) {
                infl_huff_free(&cl_huff);
                return -1;
            }
            rep += 3;
            while (rep-- && n < (int)(hlit + hdist))
                all_lens[n++] = 0;
        } else if (sym == 18) {
            uint32_t rep;
            if (infl_read_bits(b, 7, &rep) != 0) {
                infl_huff_free(&cl_huff);
                return -1;
            }
            rep += 11;
            while (rep-- && n < (int)(hlit + hdist))
                all_lens[n++] = 0;
        } else {
            infl_huff_free(&cl_huff);
            return -1;
        }
    }
    infl_huff_free(&cl_huff);

    if (infl_huff_build(all_lens, (int)hlit, lit) != 0)
        return -1;
    if (infl_huff_build(all_lens + hlit, (int)hdist, dist) != 0) {
        infl_huff_free(lit);
        return -1;
    }
    return 0;
}

int inflate_lite(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                 size_t *out_len) {
    infl_bits b;
    infl_huff lit, dist;
    size_t out_pos = 0;
    uint32_t final = 0;

    if (!src || !dst || !out_len)
        return -1;
    *out_len = 0;
    memset(&b, 0, sizeof(b));
    b.in = src;
    b.in_len = src_len;

    while (!final) {
        uint32_t btype;
        memset(&lit, 0, sizeof(lit));
        memset(&dist, 0, sizeof(dist));

        if (infl_read_bits(&b, 1, &final) != 0)
            return -1;
        if (infl_read_bits(&b, 2, &btype) != 0)
            return -1;

        if (btype == 0) {
            uint32_t len, nlen;
            infl_align_byte(&b);
            if (b.byte_pos + 4 > b.in_len)
                return -1;
            len = (uint32_t)b.in[b.byte_pos] | ((uint32_t)b.in[b.byte_pos + 1] << 8);
            nlen = (uint32_t)b.in[b.byte_pos + 2] | ((uint32_t)b.in[b.byte_pos + 3] << 8);
            b.byte_pos += 4;
            if ((len ^ 0xffffu) != nlen)
                return -1;
            if (b.byte_pos + len > b.in_len || out_pos + len > dst_cap)
                return -1;
            memcpy(dst + out_pos, b.in + b.byte_pos, len);
            b.byte_pos += len;
            out_pos += len;
        } else if (btype == 1) {
            if (infl_fixed_tables(&lit, &dist) != 0)
                return -1;
            if (infl_decode_block(&b, &lit, &dist, dst, dst_cap, &out_pos) != 0) {
                infl_huff_free(&lit);
                infl_huff_free(&dist);
                return -1;
            }
            infl_huff_free(&lit);
            infl_huff_free(&dist);
        } else if (btype == 2) {
            if (infl_dynamic_tables(&b, &lit, &dist) != 0)
                return -1;
            if (infl_decode_block(&b, &lit, &dist, dst, dst_cap, &out_pos) != 0) {
                infl_huff_free(&lit);
                infl_huff_free(&dist);
                return -1;
            }
            infl_huff_free(&lit);
            infl_huff_free(&dist);
        } else {
            return -1;
        }
    }

    *out_len = out_pos;
    return 0;
}

int inflate_lite_zlib(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                      size_t *out_len) {
    size_t raw_len;
    if (!src || src_len < 6)
        return -1;
    if ((src[0] & 0x0f) != 8)
        return -1;
    if ((src[0] >> 4) > 7)
        return -1;
    if (((uint16_t)src[0] << 8 | src[1]) % 31)
        return -1;
    raw_len = src_len - 6;
    return inflate_lite(src + 2, raw_len, dst, dst_cap, out_len);
}
