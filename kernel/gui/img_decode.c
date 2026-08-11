#include "img_decode.h"
#include "heap.h"
#include "inflate_lite.h"
#include "util.h"
#include "vfs.h"

#define IMG_MAX_DIM     1024u
#define IMG_MAX_PIXELS  (512u * 1024u / 3u) /* matches IMG_MAX_RGB / 3 */
#define IMG_MAX_RGB     (512u * 1024u)

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Reject oversized dims before any kmalloc of pixel / inflate buffers. */
static int img_dims_ok(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0 || w > IMG_MAX_DIM || h > IMG_MAX_DIM)
        return 0;
    if ((uint64_t)w * (uint64_t)h > (uint64_t)IMG_MAX_PIXELS)
        return 0;
    if ((uint64_t)w * (uint64_t)h * 3u > IMG_MAX_RGB)
        return 0;
    return 1;
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void img_decode_free(struct img_decoded *img) {
    if (!img)
        return;
    if (img->rgb)
        kfree(img->rgb);
    img->rgb = NULL;
    img->rgb_len = 0;
    img->w = img->h = 0;
}

int img_decode_ppm(const uint8_t *data, size_t len, struct img_decoded *out) {
    if (!data || !out || len < 16 || data[0] != 'P' || data[1] != '6')
        return -1;
    size_t i = 2;
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] == '#') {
        while (i < len && data[i] != '\n')
            i++;
        while (i < len && is_space((char)data[i]))
            i++;
    }
    uint32_t w = 0, h = 0, maxv = 0;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        w = w * 10 + (uint32_t)(data[i++] - '0');
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        h = h * 10 + (uint32_t)(data[i++] - '0');
    while (i < len && is_space((char)data[i]))
        i++;
    while (i < len && data[i] >= '0' && data[i] <= '9')
        maxv = maxv * 10 + (uint32_t)(data[i++] - '0');
    if (i >= len || !is_space((char)data[i]) || !img_dims_ok(w, h) || maxv != 255)
        return -1;
    i++;
    size_t need = (size_t)w * (size_t)h * 3;
    if (i + need > len)
        return -1;
    uint8_t *rgb = (uint8_t *)kmalloc(need);
    if (!rgb)
        return -1;
    memcpy(rgb, data + i, need);
    out->w = w;
    out->h = h;
    out->rgb = rgb;
    out->rgb_len = need;
    return 0;
}

int img_decode_bmp(const uint8_t *data, size_t len, struct img_decoded *out) {
    if (!data || !out || len < 54)
        return -1;
    if (data[0] != 'B' || data[1] != 'M')
        return -1;
    uint32_t off = (uint32_t)data[10] | ((uint32_t)data[11] << 8) |
                   ((uint32_t)data[12] << 16) | ((uint32_t)data[13] << 24);
    int32_t dib = (int32_t)((uint32_t)data[14] | ((uint32_t)data[15] << 8) |
                            ((uint32_t)data[16] << 16) | ((uint32_t)data[17] << 24));
    if (dib < 40)
        return -1;
    int32_t w = (int32_t)((uint32_t)data[18] | ((uint32_t)data[19] << 8) |
                          ((uint32_t)data[20] << 16) | ((uint32_t)data[21] << 24));
    int32_t h = (int32_t)((uint32_t)data[22] | ((uint32_t)data[23] << 8) |
                          ((uint32_t)data[24] << 16) | ((uint32_t)data[25] << 24));
    uint16_t bpp = (uint16_t)(data[28] | (data[29] << 8));
    uint32_t comp = (uint32_t)data[30] | ((uint32_t)data[31] << 8) |
                    ((uint32_t)data[32] << 16) | ((uint32_t)data[33] << 24);
    if (w <= 0 || h == 0 || (bpp != 24 && bpp != 32) || comp != 0)
        return -1;
    int top_down = h < 0;
    uint32_t uw = (uint32_t)(w < 0 ? -w : w);
    uint32_t uh = (uint32_t)(h < 0 ? -h : h);
    if (off >= len || !img_dims_ok(uw, uh))
        return -1;
    size_t need = (size_t)uw * (size_t)uh * 3;
    uint8_t *rgb = (uint8_t *)kmalloc(need);
    if (!rgb)
        return -1;
    uint32_t row_bytes = ((uw * (uint32_t)bpp + 31) / 32) * 4;
    const uint8_t *src = data + off;
    for (uint32_t row = 0; row < uh; row++) {
        uint32_t sy = top_down ? row : (uh - 1 - row);
        if (off + sy * row_bytes + row_bytes > len) {
            kfree(rgb);
            return -1;
        }
        const uint8_t *srow = src + sy * row_bytes;
        uint8_t *drow = rgb + (size_t)row * (size_t)uw * 3;
        for (uint32_t x = 0; x < uw; x++) {
            const uint8_t *p = srow + (size_t)x * (bpp / 8);
            drow[x * 3 + 0] = p[2];
            drow[x * 3 + 1] = p[1];
            drow[x * 3 + 2] = p[0];
        }
    }
    out->w = uw;
    out->h = uh;
    out->rgb = rgb;
    out->rgb_len = need;
    return 0;
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p - (int)a;
    int pb = p - (int)b;
    int pc = p - (int)c;
    if (pa < 0)
        pa = -pa;
    if (pb < 0)
        pb = -pb;
    if (pc < 0)
        pc = -pc;
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

static void png_unfilter_row(uint8_t *row, const uint8_t *prev, uint32_t row_bytes,
                             uint8_t filter) {
    uint32_t i;
    if (filter == 0)
        return;
    for (i = 0; i < row_bytes; i++) {
        uint8_t x = row[i];
        uint8_t a = i > 0 ? row[i - 1] : 0;
        uint8_t b = prev ? prev[i] : 0;
        uint8_t c = (i > 0 && prev) ? prev[i - 1] : 0;
        switch (filter) {
        case 1:
            row[i] = (uint8_t)(x + a);
            break;
        case 2:
            row[i] = (uint8_t)(x + b);
            break;
        case 3:
            row[i] = (uint8_t)(x + (a + b) / 2);
            break;
        case 4:
            row[i] = (uint8_t)(x + png_paeth(a, b, c));
            break;
        default:
            break;
        }
    }
}

static void png_pixel_rgb(uint8_t *dst, const uint8_t *src, int ctype, int has_trns,
                          uint8_t trns[6]) {
    uint8_t r, g, b, a = 255;
    switch (ctype) {
    case 0:
        r = g = b = src[0];
        if (has_trns && r == trns[0])
            a = 0;
        break;
    case 2:
        r = src[0];
        g = src[1];
        b = src[2];
        if (has_trns && r == trns[0] && g == trns[1] && b == trns[2])
            a = 0;
        break;
    case 4:
        r = g = b = src[0];
        a = src[1];
        break;
    case 6:
        r = src[0];
        g = src[1];
        b = src[2];
        a = src[3];
        break;
    default:
        r = g = b = 0;
        break;
    }
    if (a < 255) {
        dst[0] = (uint8_t)((r * a + 255 * (255 - a) + 127) / 255);
        dst[1] = (uint8_t)((g * a + 255 * (255 - a) + 127) / 255);
        dst[2] = (uint8_t)((b * a + 255 * (255 - a) + 127) / 255);
    } else {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
    }
}

int img_decode_png(const uint8_t *data, size_t len, struct img_decoded *out) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    uint32_t w = 0, h = 0;
    uint8_t bit_depth = 0, color_type = 0, interlace = 0;
    uint8_t trns[6];
    int has_trns = 0;
    size_t idat_cap = 0, idat_len = 0;
    uint8_t *idat = NULL;
    size_t i;

    if (!data || !out || len < 8 || memcmp(data, sig, 8) != 0)
        return -1;

    i = 8;
    while (i + 12 <= len) {
        uint32_t clen = be32(data + i);
        const uint8_t *tag = data + i + 4;
        const uint8_t *payload = data + i + 8;
        i += 12 + clen;
        if (i > len)
            return -1;

        if (tag[0] == 'I' && tag[1] == 'H' && tag[2] == 'D' && tag[3] == 'R') {
            if (clen != 13)
                return -1;
            w = be32(payload);
            h = be32(payload + 4);
            bit_depth = payload[8];
            color_type = payload[9];
            interlace = payload[12];
            if (bit_depth != 8 || interlace != 0)
                return -1;
            if (color_type != 0 && color_type != 2 && color_type != 4 && color_type != 6)
                return -1;
            if (!img_dims_ok(w, h))
                return -1;
        } else if (tag[0] == 'I' && tag[1] == 'D' && tag[2] == 'A' && tag[3] == 'T') {
            if (clen == 0)
                continue;
            if (idat_len + clen > idat_cap) {
                size_t ncap = idat_cap ? idat_cap * 2 : 256;
                while (ncap < idat_len + clen)
                    ncap *= 2;
                {
                    uint8_t *nbuf = (uint8_t *)kmalloc(ncap);
                    if (!nbuf)
                        goto png_fail;
                    if (idat)
                        memcpy(nbuf, idat, idat_len);
                    kfree(idat);
                    idat = nbuf;
                    idat_cap = ncap;
                }
            }
            memcpy(idat + idat_len, payload, clen);
            idat_len += clen;
        } else if (tag[0] == 't' && tag[1] == 'R' && tag[2] == 'N' && tag[3] == 'S') {
            if (color_type == 0 && clen >= 1) {
                trns[0] = payload[0];
                has_trns = 1;
            } else if (color_type == 2 && clen >= 6) {
                trns[0] = payload[0];
                trns[1] = payload[2];
                trns[2] = payload[4];
                has_trns = 1;
            }
        } else if (tag[0] == 'I' && tag[1] == 'E' && tag[2] == 'N' && tag[3] == 'D') {
            break;
        }
    }

    if (w == 0 || h == 0 || !idat || idat_len == 0)
        goto png_fail;

    {
        int spp = (color_type == 2) ? 3 : (color_type == 6) ? 4 : (color_type == 4) ? 2 : 1;
        size_t raw_need = (size_t)h * ((size_t)w * (size_t)spp + 1u);
        size_t raw_len = 0;
        uint8_t *raw = (uint8_t *)kmalloc(raw_need);
        uint8_t *prev = NULL;
        uint8_t *rgb = NULL;
        size_t y;

        if (!raw)
            goto png_fail;
        if (inflate_lite_zlib(idat, idat_len, raw, raw_need, &raw_len) != 0) {
            kfree(raw);
            goto png_fail;
        }
        if (raw_len != raw_need) {
            kfree(raw);
            goto png_fail;
        }

        prev = (uint8_t *)kmalloc((size_t)w * (size_t)spp);
        rgb = (uint8_t *)kmalloc((size_t)w * (size_t)h * 3);
        if (!prev || !rgb) {
            kfree(raw);
            kfree(prev);
            kfree(rgb);
            goto png_fail;
        }
        memset(prev, 0, (size_t)w * (size_t)spp);

        {
            size_t off = 0;
            for (y = 0; y < h; y++) {
                uint8_t filter = raw[off++];
                uint8_t *row = raw + off;
                uint32_t rb = (uint32_t)w * (uint32_t)spp;
                png_unfilter_row(row, prev, rb, filter);
                {
                    uint32_t x;
                    for (x = 0; x < w; x++)
                        png_pixel_rgb(rgb + ((size_t)y * w + x) * 3, row + x * spp, color_type,
                                      has_trns, trns);
                }
                memcpy(prev, row, rb);
                off += rb;
            }
        }

        kfree(raw);
        kfree(prev);
        kfree(idat);
        out->w = w;
        out->h = h;
        out->rgb = rgb;
        out->rgb_len = (size_t)w * (size_t)h * 3;
        return 0;
    }

png_fail:
    kfree(idat);
    return -1;
}

/* --- Baseline JPEG (SOF0, 8-bit) --- */

static const int jpeg_zigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63
};

typedef struct {
    uint8_t bits[17];
    uint8_t syms[256];
    int nsyms;
    int mincode[17];
    int maxcode[17];
    int valptr[17];
} jpeg_huff;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint32_t buf;
    int cnt;
} jpeg_bits;

typedef struct {
    const uint8_t *data;
    size_t len;
    uint32_t w, h;
    int ncomp;
    struct {
        uint8_t id;
        uint8_t hv;
        uint8_t qt;
    } comp[4];
    int qt_used[4];
    int16_t qtab[4][64];
    jpeg_huff dc[4];
    jpeg_huff ac[4];
    int dc_sel[4];
    int ac_sel[4];
    uint8_t *rgb;
} jpeg_ctx;

static int jpeg_fill(jpeg_bits *b) {
    while (b->cnt <= 24 && b->pos < b->len) {
        uint8_t c = b->data[b->pos++];
        if (c == 0xff) {
            if (b->pos >= b->len)
                return -1;
            if (b->data[b->pos] == 0x00)
                b->pos++;
            else if (b->data[b->pos] >= 0xd0 && b->data[b->pos] <= 0xd7)
                b->pos++;
            else
                return 1;
        }
        b->buf = (b->buf << 8) | c;
        b->cnt += 8;
    }
    return 0;
}

static int jpeg_read_bits(jpeg_bits *b, int n, int *out) {
    if (n <= 0 || n > 16)
        return -1;
    if (jpeg_fill(b) != 0 && b->cnt < n)
        return -1;
    if (n > b->cnt)
        return -1;
    *out = (int)((b->buf >> (b->cnt - n)) & ((1 << n) - 1));
    b->cnt -= n;
    return 0;
}

static int jpeg_huff_load(jpeg_huff *h, const uint8_t *counts, const uint8_t *syms, int nsyms) {
    int code = 0, k = 0, i;
    memset(h, 0, sizeof(*h));
    memcpy(h->bits + 1, counts, 16);
    for (i = 0; i < nsyms; i++)
        h->syms[i] = syms[i];
    h->nsyms = nsyms;
    for (i = 1; i <= 16; i++) {
        if (h->bits[i]) {
            h->mincode[i] = code;
            h->maxcode[i] = code + h->bits[i] - 1;
            h->valptr[i] = k;
            k += h->bits[i];
            code += h->bits[i];
        } else {
            h->mincode[i] = -1;
            h->maxcode[i] = -1;
        }
        code <<= 1;
    }
    return 0;
}

static int jpeg_huff_decode(jpeg_bits *b, jpeg_huff *h, int *out_sym) {
    int code = 0, k;
    for (k = 1; k <= 16; k++) {
        int bit;
        if (jpeg_read_bits(b, 1, &bit) != 0)
            return -1;
        code = (code << 1) | bit;
        if (h->maxcode[k] >= 0 && code <= h->maxcode[k]) {
            *out_sym = h->syms[h->valptr[k] + code - h->mincode[k]];
            return 0;
        }
    }
    return -1;
}

static void jpeg_idct8(int16_t *d) {
    int i, ac = 0;
    for (i = 1; i < 64; i++) {
        if (d[i]) {
            ac = 1;
            break;
        }
    }
    if (!ac) {
        int v = (d[0] + 4) >> 3;
        for (i = 0; i < 64; i++)
            d[i] = (int16_t)v;
        return;
    }
    {
        int32_t tmp[64];
        static const int16_t c1 = 284;
        static const int16_t c2 = 946;
        static const int16_t c3 = 569;
        static const int16_t c5 = 784;
        static const int16_t c6 = 401;
        static const int16_t c7 = 112;

        for (i = 0; i < 64; i++)
            tmp[i] = d[i];
        for (i = 0; i < 8; i++) {
            int32_t *row = tmp + i * 8;
            int32_t t0 = row[0] + row[4];
            int32_t t1 = row[0] - row[4];
            int32_t t2 = (row[2] * c6 + row[6] * c2) >> 10;
            int32_t t3 = (row[2] * c2 - row[6] * c6) >> 10;
            int32_t t4 = row[1] + row[3] + row[5] + row[7];
            int32_t t5 = ((row[1] - row[7]) * c7 + (row[3] - row[5]) * c3) >> 10;
            int32_t t6 = ((row[1] + row[7]) * c1 + (row[3] + row[5]) * c5) >> 10;
            int32_t t7 = ((row[3] + row[5]) * c1 - (row[1] + row[7]) * c5) >> 10;
            row[0] = t0 + t2 + t6;
            row[1] = t0 - t2 + t5;
            row[2] = t1 + t3 + t7;
            row[3] = t1 - t3 + t4 - t6 - t7;
            row[4] = t0 + t2 - t6;
            row[5] = t0 - t2 - t5;
            row[6] = t1 + t3 - t7;
            row[7] = t1 - t3 - t4 + t6 + t7;
        }
        for (i = 0; i < 8; i++)
            for (int j = 0; j < 8; j++)
                d[i * 8 + j] = (int16_t)tmp[j * 8 + i];
        for (i = 0; i < 8; i++) {
            int16_t *row = d + i * 8;
            int32_t r0 = row[0], r1 = row[1], r2 = row[2], r3 = row[3];
            int32_t r4 = row[4], r5 = row[5], r6 = row[6], r7 = row[7];
            int32_t t0 = r0 + r4;
            int32_t t1 = r0 - r4;
            int32_t t2 = (r2 * c6 + r6 * c2) >> 10;
            int32_t t3 = (r2 * c2 - r6 * c6) >> 10;
            int32_t t4 = r1 + r3 + r5 + r7;
            int32_t t5 = ((r1 - r7) * c7 + (r3 - r5) * c3) >> 10;
            int32_t t6 = ((r1 + r7) * c1 + (r3 + r5) * c5) >> 10;
            int32_t t7 = ((r3 + r5) * c1 - (r1 + r7) * c5) >> 10;
            row[0] = (int16_t)((t0 + t2 + t6 + 8) >> 4);
            row[1] = (int16_t)((t0 - t2 + t5 + 8) >> 4);
            row[2] = (int16_t)((t1 + t3 + t7 + 8) >> 4);
            row[3] = (int16_t)((t1 - t3 + t4 - t6 - t7 + 8) >> 4);
            row[4] = (int16_t)((t0 + t2 - t6 + 8) >> 4);
            row[5] = (int16_t)((t0 - t2 - t5 + 8) >> 4);
            row[6] = (int16_t)((t1 + t3 - t7 + 8) >> 4);
            row[7] = (int16_t)((t1 - t3 - t4 + t6 + t7 + 8) >> 4);
        }
    }
}

static int jpeg_decode_block(jpeg_bits *bits, jpeg_huff *dc_h, jpeg_huff *ac_h,
                             int16_t *prev_dc, int16_t *out, const int16_t *qt) {
    int s, i, k = 1;
    if (jpeg_huff_decode(bits, dc_h, &s) != 0)
        return -1;
    if (s > 0) {
        int diff;
        if (jpeg_read_bits(bits, s, &diff) != 0)
            return -1;
        if (diff < (1 << (s - 1)))
            diff -= (1 << s) - 1;
        *prev_dc = (int16_t)(*prev_dc + diff);
    }
    out[0] = (int16_t)(*prev_dc * qt[0]);
    for (i = 1; i < 64; i++)
        out[i] = 0;
    while (k < 64) {
        int rs;
        if (jpeg_huff_decode(bits, ac_h, &rs) != 0)
            return -1;
        if (rs == 0)
            break;
        {
            int r = rs >> 4;
            s = rs & 15;
            k += r;
            if (k >= 64)
                break;
            if (s > 0) {
                int val;
                if (jpeg_read_bits(bits, s, &val) != 0)
                    return -1;
                if (val < (1 << (s - 1)))
                    val -= (1 << s) - 1;
                out[jpeg_zigzag[k]] = (int16_t)(val * qt[jpeg_zigzag[k]]);
            }
            k++;
        }
    }
    jpeg_idct8(out);
    for (i = 0; i < 64; i++) {
        int v = out[i] + 128;
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        out[i] = (int16_t)v;
    }
    return 0;
}

static void jpeg_ycbcr_to_rgb(uint8_t *dst, int y, int cb, int cr) {
    int c = y - 16, d = cb - 128, e = cr - 128;
    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    dst[0] = (uint8_t)r;
    dst[1] = (uint8_t)g;
    dst[2] = (uint8_t)b;
}

static int jpeg_sample_comp(int16_t comp_blocks[][64], int hi, int vi, int max_h, int max_v,
                            int x, int y) {
    int w = x * hi / max_h;
    int h = y * vi / max_v;
    int bx = w / 8;
    int by = h / 8;
    int bi = by * hi + bx;
    return comp_blocks[bi][(h & 7) * 8 + (w & 7)];
}

static int jpeg_decode_scan(jpeg_ctx *ctx, jpeg_bits *bits, size_t scan_off) {
    int16_t dc[4] = {0, 0, 0, 0};
    int mcu_w = 8, mcu_h = 8;
    int max_h = 1, max_v = 1;
    int ci;

    for (ci = 0; ci < ctx->ncomp; ci++) {
        int hv = ctx->comp[ci].hv;
        int hh = (hv >> 4) & 0x0f;
        int vv = hv & 0x0f;
        if (hh > max_h)
            max_h = hh;
        if (vv > max_v)
            max_v = vv;
    }
    mcu_w = max_h * 8;
    mcu_h = max_v * 8;

    bits->data = ctx->data;
    bits->len = ctx->len;
    bits->pos = scan_off;
    bits->buf = 0;
    bits->cnt = 0;

    {
        uint32_t mcu_cols = (ctx->w + (uint32_t)mcu_w - 1) / (uint32_t)mcu_w;
        uint32_t mcu_rows = (ctx->h + (uint32_t)mcu_h - 1) / (uint32_t)mcu_h;
        uint32_t my, mx;

        for (my = 0; my < mcu_rows; my++) {
            for (mx = 0; mx < mcu_cols; mx++) {
                int16_t blocks[3][6][64];
                int comp_idx;
                memset(blocks, 0, sizeof(blocks));
                for (comp_idx = 0; comp_idx < ctx->ncomp; comp_idx++) {
                    int hv = ctx->comp[comp_idx].hv;
                    int hh = (hv >> 4) & 0x0f;
                    int vv = hv & 0x0f;
                    int qt = ctx->comp[comp_idx].qt;
                    int bi, bj;
                    for (bj = 0; bj < vv; bj++) {
                        for (bi = 0; bi < hh; bi++) {
                            int blk = bj * hh + bi;
                            if (jpeg_decode_block(bits, &ctx->dc[ctx->dc_sel[comp_idx]],
                                                  &ctx->ac[ctx->ac_sel[comp_idx]], &dc[comp_idx],
                                                  blocks[comp_idx][blk], ctx->qtab[qt]) != 0)
                                return -1;
                        }
                    }
                }
                {
                    int y;
                    for (y = 0; y < mcu_h; y++) {
                        int x;
                        for (x = 0; x < mcu_w; x++) {
                            uint32_t px = mx * (uint32_t)mcu_w + (uint32_t)x;
                            uint32_t py = my * (uint32_t)mcu_h + (uint32_t)y;
                            if (px >= ctx->w || py >= ctx->h)
                                continue;
                            if (ctx->ncomp == 1) {
                                int yy = jpeg_sample_comp(blocks[0], ctx->comp[0].hv >> 4,
                                                          ctx->comp[0].hv & 0x0f, max_h, max_v, x, y);
                                uint8_t *d = ctx->rgb + ((size_t)py * ctx->w + px) * 3;
                                d[0] = d[1] = d[2] = (uint8_t)yy;
                            } else {
                                int yy = jpeg_sample_comp(blocks[0], ctx->comp[0].hv >> 4,
                                                          ctx->comp[0].hv & 0x0f, max_h, max_v, x, y);
                                int cb = jpeg_sample_comp(blocks[1], ctx->comp[1].hv >> 4,
                                                          ctx->comp[1].hv & 0x0f, max_h, max_v, x, y);
                                int cr = jpeg_sample_comp(blocks[2], ctx->comp[2].hv >> 4,
                                                          ctx->comp[2].hv & 0x0f, max_h, max_v, x, y);
                                jpeg_ycbcr_to_rgb(ctx->rgb + ((size_t)py * ctx->w + px) * 3, yy, cb,
                                                  cr);
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

int img_decode_jpeg(const uint8_t *data, size_t len, struct img_decoded *out) {
    jpeg_ctx ctx;
    size_t pos = 0;
    size_t scan_off = 0;
    int have_sof = 0;

    if (!data || !out || len < 4 || data[0] != 0xff || data[1] != 0xd8)
        return -1;
    memset(&ctx, 0, sizeof(ctx));
    pos = 2;

    while (pos + 1 < len) {
        if (data[pos] != 0xff) {
            pos++;
            continue;
        }
        while (pos < len && data[pos] == 0xff)
            pos++;
        if (pos >= len)
            break;
        {
            uint8_t marker = data[pos++];
            if (marker == 0xd9)
                break;
            if (marker == 0xda) {
                scan_off = pos;
                break;
            }
            if (marker == 0x00 || (marker >= 0xd0 && marker <= 0xd7))
                continue;
            if (pos + 2 > len)
                return -1;
            {
                uint16_t seglen = (uint16_t)((data[pos] << 8) | data[pos + 1]);
                if (seglen < 2 || pos + seglen > len)
                    return -1;
                if (marker == 0xc0 || marker == 0xc1) {
                    if (marker != 0xc0)
                        return -1;
                    if (seglen < 8 || data[pos + 2] != 8)
                        return -1;
                    ctx.h = ((uint32_t)data[pos + 3] << 8) | data[pos + 4];
                    ctx.w = ((uint32_t)data[pos + 5] << 8) | data[pos + 6];
                    ctx.ncomp = data[pos + 7];
                    if (ctx.ncomp < 1 || ctx.ncomp > 3 || seglen < 8 + (size_t)ctx.ncomp * 3)
                        return -1;
                    if (!img_dims_ok(ctx.w, ctx.h))
                        return -1;
                    {
                        int i;
                        for (i = 0; i < ctx.ncomp; i++) {
                            ctx.comp[i].id = data[pos + 8 + i * 3];
                            ctx.comp[i].hv = data[pos + 9 + i * 3];
                            ctx.comp[i].qt = data[pos + 10 + i * 3];
                        }
                    }
                    have_sof = 1;
                } else if (marker == 0xdb) {
                    size_t qpos = pos + 2;
                    while (qpos + 1 < pos + seglen) {
                        int pq = data[qpos++];
                        int idx = pq & 0x0f;
                        int i;
                        if (idx > 3)
                            return -1;
                        ctx.qt_used[idx] = 1;
                        for (i = 0; i < 64; i++)
                            ctx.qtab[idx][jpeg_zigzag[i]] = (int16_t)data[qpos + i];
                        qpos += 64;
                    }
                } else if (marker == 0xc4) {
                    size_t hpos = pos + 2;
                    while (hpos + 17 <= pos + seglen) {
                        int tc = data[hpos] >> 4;
                        int th = data[hpos] & 0x0f;
                        int ns = 0, i;
                        for (i = 0; i < 16; i++)
                            ns += data[hpos + 1 + i];
                        if (hpos + 17 + (size_t)ns > pos + seglen)
                            return -1;
                        if (th > 3)
                            return -1;
                        if (tc == 0)
                            jpeg_huff_load(&ctx.dc[th], data + hpos + 1, data + hpos + 17, ns);
                        else
                            jpeg_huff_load(&ctx.ac[th], data + hpos + 1, data + hpos + 17, ns);
                        hpos += 17 + (size_t)ns;
                    }
                } else if (marker == 0xda) {
                    (void)0;
                }
                pos += seglen;
            }
        }
    }

    if (!have_sof || scan_off == 0)
        return -1;

    {
        size_t p = scan_off;
        if (p + 1 >= len)
            return -1;
        {
            uint16_t seglen = (uint16_t)((data[p] << 8) | data[p + 1]);
            int ns = data[p + 2];
            int i;
            if (seglen < 6 || p + seglen > len || ns != ctx.ncomp)
                return -1;
            for (i = 0; i < ns; i++) {
                int cid = data[p + 3 + i * 2];
                int sel = data[p + 4 + i * 2];
                int ci;
                for (ci = 0; ci < ctx.ncomp; ci++) {
                    if (ctx.comp[ci].id == (uint8_t)cid) {
                        ctx.dc_sel[ci] = sel >> 4;
                        ctx.ac_sel[ci] = sel & 0x0f;
                        break;
                    }
                }
            }
            scan_off = p + seglen;
        }
    }

    ctx.data = data;
    ctx.len = len;
    ctx.rgb = (uint8_t *)kmalloc((size_t)ctx.w * (size_t)ctx.h * 3);
    if (!ctx.rgb)
        return -1;
    memset(ctx.rgb, 0, (size_t)ctx.w * (size_t)ctx.h * 3);

    {
        jpeg_bits bits;
        if (jpeg_decode_scan(&ctx, &bits, scan_off) != 0) {
            kfree(ctx.rgb);
            return -1;
        }
    }

    out->w = ctx.w;
    out->h = ctx.h;
    out->rgb = ctx.rgb;
    out->rgb_len = (size_t)ctx.w * (size_t)ctx.h * 3;
    return 0;
}

int img_decode_mem(const uint8_t *data, size_t len, struct img_decoded *out) {
    if (!data || !out || len < 2)
        return -1;
    if (len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return img_decode_png(data, len, out);
    if (data[0] == 0xff && data[1] == 0xd8)
        return img_decode_jpeg(data, len, out);
    if (data[0] == 'P' && data[1] == '6')
        return img_decode_ppm(data, len, out);
    if (data[0] == 'B' && data[1] == 'M')
        return img_decode_bmp(data, len, out);
    return -1;
}

int img_decode_file(const char *path, struct img_decoded *out) {
    if (!path || !out)
        return -1;
    struct vfs_node *n = vfs_lookup(path);
    if (!n || n->type != VFS_FILE || !n->data || n->size < 8)
        return -1;
    size_t nl = strlen(path);
    if (nl >= 4) {
        if (!strcmp(path + nl - 4, ".bmp"))
            return img_decode_bmp(n->data, n->size, out);
        if (!strcmp(path + nl - 4, ".ppm"))
            return img_decode_ppm(n->data, n->size, out);
        if (!strcmp(path + nl - 4, ".png"))
            return img_decode_png(n->data, n->size, out);
        if (!strcmp(path + nl - 4, ".jpg"))
            return img_decode_jpeg(n->data, n->size, out);
    }
    if (nl >= 5 && !strcmp(path + nl - 5, ".jpeg"))
        return img_decode_jpeg(n->data, n->size, out);
    return img_decode_mem(n->data, n->size, out);
}
