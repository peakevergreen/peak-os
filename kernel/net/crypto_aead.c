/*
 * crypto_aead.c — AES-128-GCM and ChaCha20-Poly1305 for TLS 1.2 + PeakDisk.
 * See also crypto_hash.c, crypto_x25519.c, crypto.c (RNG glue).
 */
#include "crypto.h"
#include "util.h"

/* ---- AES-128 ---- */

static void wrbe64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static void aes_key_expand(const uint8_t key[16], uint8_t rk[176]) {
    memcpy(rk, key, 16);
    for (int i = 4; i < 44; i++) {
        uint8_t t[4];
        memcpy(t, rk + (i - 1) * 4, 4);
        if (i % 4 == 0) {
            uint8_t tmp = t[0];
            t[0] = sbox[t[1]] ^ rcon[i / 4];
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[tmp];
        }
        for (int j = 0; j < 4; j++)
            rk[i * 4 + j] = rk[(i - 4) * 4 + j] ^ t[j];
    }
}

static void aes_add_round_key(uint8_t s[16], const uint8_t *rk) {
    for (int i = 0; i < 16; i++)
        s[i] ^= rk[i];
}
static void aes_sub_bytes(uint8_t s[16]) {
    for (int i = 0; i < 16; i++)
        s[i] = sbox[s[i]];
}
static void aes_shift_rows(uint8_t s[16]) {
    uint8_t t;
    t=s[1]; s[1]=s[5]; s[5]=s[9]; s[9]=s[13]; s[13]=t;
    t=s[2]; s[2]=s[10]; s[10]=t; t=s[6]; s[6]=s[14]; s[14]=t;
    t=s[15]; s[15]=s[11]; s[11]=s[7]; s[7]=s[3]; s[3]=t;
}
static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1b : 0));
}
static void aes_mix_columns(uint8_t s[16]) {
    for (int c = 0; c < 4; c++) {
        uint8_t *p = s + c * 4;
        uint8_t a=p[0],b=p[1],c0=p[2],d=p[3];
        p[0] = (uint8_t)(xtime(a)^xtime(b)^b^c0^d);
        p[1] = (uint8_t)(a^xtime(b)^xtime(c0)^c0^d);
        p[2] = (uint8_t)(a^b^xtime(c0)^xtime(d)^d);
        p[3] = (uint8_t)(xtime(a)^a^b^c0^xtime(d));
    }
}

void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    uint8_t rk[176], s[16];
    aes_key_expand(key, rk);
    memcpy(s, in, 16);
    aes_add_round_key(s, rk);
    for (int r = 1; r < 10; r++) {
        aes_sub_bytes(s);
        aes_shift_rows(s);
        aes_mix_columns(s);
        aes_add_round_key(s, rk + r * 16);
    }
    aes_sub_bytes(s);
    aes_shift_rows(s);
    aes_add_round_key(s, rk + 160);
    memcpy(out, s, 16);
}

/* ---- AES-GCM (compact) ---- */

static void gcm_mult(uint8_t x[16], const uint8_t y[16]) {
    uint8_t z[16], v[16];
    memset(z, 0, 16);
    memcpy(v, y, 16);
    for (int i = 0; i < 128; i++) {
        if (x[i / 8] & (1u << (7 - (i % 8)))) {
            for (int j = 0; j < 16; j++)
                z[j] ^= v[j];
        }
        uint8_t lsb = v[15] & 1;
        for (int j = 15; j > 0; j--)
            v[j] = (uint8_t)((v[j] >> 1) | ((v[j - 1] & 1) << 7));
        v[0] >>= 1;
        if (lsb)
            v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void gcm_inc32(uint8_t ctr[16]) {
    for (int i = 15; i >= 12; i--) {
        if (++ctr[i])
            break;
    }
}

static void ghash(uint8_t s[16], const uint8_t h[16], const uint8_t *a, size_t a_len,
                  const uint8_t *c, size_t c_len) {
    memset(s, 0, 16);
    size_t i;
    for (i = 0; i + 16 <= a_len; i += 16) {
        for (int j = 0; j < 16; j++)
            s[j] ^= a[i + j];
        gcm_mult(s, h);
    }
    if (a_len > i) {
        uint8_t blk[16];
        memset(blk, 0, 16);
        memcpy(blk, a + i, a_len - i);
        for (int j = 0; j < 16; j++)
            s[j] ^= blk[j];
        gcm_mult(s, h);
    }
    for (i = 0; i + 16 <= c_len; i += 16) {
        for (int j = 0; j < 16; j++)
            s[j] ^= c[i + j];
        gcm_mult(s, h);
    }
    if (c_len > i) {
        uint8_t blk[16];
        memset(blk, 0, 16);
        memcpy(blk, c + i, c_len - i);
        for (int j = 0; j < 16; j++)
            s[j] ^= blk[j];
        gcm_mult(s, h);
    }
    uint8_t lenblk[16];
    memset(lenblk, 0, 16);
    wrbe64(lenblk, (uint64_t)a_len * 8);
    wrbe64(lenblk + 8, (uint64_t)c_len * 8);
    for (int j = 0; j < 16; j++)
        s[j] ^= lenblk[j];
    gcm_mult(s, h);
}

int aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *plain, size_t plain_len,
                       uint8_t *cipher, uint8_t tag[16]) {
    uint8_t h[16], j0[16], ctr[16], s[16];
    memset(h, 0, 16);
    aes128_encrypt_block(key, h, h);
    memset(j0, 0, 16);
    memcpy(j0, iv, 12);
    j0[15] = 1;
    memcpy(ctr, j0, 16);
    size_t off = 0;
    while (off < plain_len) {
        gcm_inc32(ctr);
        uint8_t ks[16];
        aes128_encrypt_block(key, ctr, ks);
        size_t n = plain_len - off;
        if (n > 16)
            n = 16;
        for (size_t i = 0; i < n; i++)
            cipher[off + i] = plain[off + i] ^ ks[i];
        off += n;
    }
    ghash(s, h, aad, aad_len, cipher, plain_len);
    uint8_t t[16];
    aes128_encrypt_block(key, j0, t);
    for (int i = 0; i < 16; i++)
        tag[i] = t[i] ^ s[i];
    return 0;
}

int aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *cipher, size_t cipher_len,
                       const uint8_t tag[16], uint8_t *plain) {
    uint8_t h[16], j0[16], ctr[16], s[16], t[16], expect[16];
    memset(h, 0, 16);
    aes128_encrypt_block(key, h, h);
    memset(j0, 0, 16);
    memcpy(j0, iv, 12);
    j0[15] = 1;
    ghash(s, h, aad, aad_len, cipher, cipher_len);
    aes128_encrypt_block(key, j0, t);
    for (int i = 0; i < 16; i++)
        expect[i] = t[i] ^ s[i];
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= expect[i] ^ tag[i];
    if (diff)
        return -1;
    memcpy(ctr, j0, 16);
    size_t off = 0;
    while (off < cipher_len) {
        gcm_inc32(ctr);
        uint8_t ks[16];
        aes128_encrypt_block(key, ctr, ks);
        size_t n = cipher_len - off;
        if (n > 16)
            n = 16;
        for (size_t i = 0; i < n; i++)
            plain[off + i] = cipher[off + i] ^ ks[i];
        off += n;
    }
    return 0;
}

/* ---- ChaCha20-Poly1305 (RFC 7539) ---- */

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wrle32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void chacha20_quarter(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

static void chacha20_block(const uint8_t key[32], const uint8_t nonce[12],
                           uint32_t counter, uint8_t out[64]) {
    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state[4 + i] = le32(key + i * 4);
    state[12] = counter;
    state[13] = le32(nonce);
    state[14] = le32(nonce + 4);
    state[15] = le32(nonce + 8);
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = state[i];
    for (int i = 0; i < 10; i++) {
        chacha20_quarter(&w[0], &w[4], &w[8], &w[12]);
        chacha20_quarter(&w[1], &w[5], &w[9], &w[13]);
        chacha20_quarter(&w[2], &w[6], &w[10], &w[14]);
        chacha20_quarter(&w[3], &w[7], &w[11], &w[15]);
        chacha20_quarter(&w[0], &w[5], &w[10], &w[15]);
        chacha20_quarter(&w[1], &w[6], &w[11], &w[12]);
        chacha20_quarter(&w[2], &w[7], &w[8], &w[13]);
        chacha20_quarter(&w[3], &w[4], &w[9], &w[14]);
    }
    for (int i = 0; i < 16; i++)
        wrle32(out + i * 4, w[i] + state[i]);
}

static void chacha20_xor(const uint8_t key[32], const uint8_t nonce[12],
                         uint32_t counter, const uint8_t *in, size_t len, uint8_t *out) {
    size_t off = 0;
    while (off < len) {
        uint8_t block[64];
        chacha20_block(key, nonce, counter++, block);
        size_t n = len - off;
        if (n > 64)
            n = 64;
        for (size_t i = 0; i < n; i++)
            out[off + i] = in[off + i] ^ block[i];
        off += n;
    }
}

struct poly1305 {
    uint32_t r0, r1, r2, r3, r4;
    uint32_t h0, h1, h2, h3, h4;
    uint8_t buf[16];
    size_t buf_len;
    uint8_t s[16];
};

static void poly1305_init(struct poly1305 *p, const uint8_t key[32]) {
    p->r0 = le32(key) & 0x3ffffffu;
    p->r1 = (le32(key + 3) >> 2) & 0x3ffff03u;
    p->r2 = (le32(key + 6) >> 4) & 0x3ffc0ffu;
    p->r3 = (le32(key + 9) >> 6) & 0x3f03fffu;
    p->r4 = (le32(key + 12) >> 8) & 0x00fffffu;
    p->h0 = p->h1 = p->h2 = p->h3 = p->h4 = 0;
    p->buf_len = 0;
    memcpy(p->s, key + 16, 16);
}

static void poly1305_blocks(struct poly1305 *p, const uint8_t *m, size_t nblocks, int final) {
    uint32_t r0 = p->r0, r1 = p->r1, r2 = p->r2, r3 = p->r3, r4 = p->r4;
    uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = p->h0, h1 = p->h1, h2 = p->h2, h3 = p->h3, h4 = p->h4;
    uint32_t hibit = final ? 0 : (1u << 24);

    while (nblocks--) {
        uint32_t t0 = le32(m) & 0x3ffffffu;
        uint32_t t1 = (le32(m + 3) >> 2) & 0x3ffffffu;
        uint32_t t2 = (le32(m + 6) >> 4) & 0x3ffffffu;
        uint32_t t3 = (le32(m + 9) >> 6) & 0x3ffffffu;
        uint32_t t4 = (le32(m + 12) >> 8) | hibit;
        m += 16;

        uint64_t a0 = h0 + t0;
        uint64_t a1 = h1 + t1;
        uint64_t a2 = h2 + t2;
        uint64_t a3 = h3 + t3;
        uint64_t a4 = h4 + t4;
        uint64_t x0 = a0 * r0 + a1 * s4 + a2 * s3 + a3 * s2 + a4 * s1;
        uint64_t x1 = a0 * r1 + a1 * r0 + a2 * s4 + a3 * s3 + a4 * s2;
        uint64_t x2 = a0 * r2 + a1 * r1 + a2 * r0 + a3 * s4 + a4 * s3;
        uint64_t x3 = a0 * r3 + a1 * r2 + a2 * r1 + a3 * r0 + a4 * s4;
        uint64_t x4 = a0 * r4 + a1 * r3 + a2 * r2 + a3 * r1 + a4 * r0;
        uint64_t c;
        h0 = (uint32_t)(x0 & 0x3ffffffu); c = x0 >> 26;
        x1 += c; h1 = (uint32_t)(x1 & 0x3ffffffu); c = x1 >> 26;
        x2 += c; h2 = (uint32_t)(x2 & 0x3ffffffu); c = x2 >> 26;
        x3 += c; h3 = (uint32_t)(x3 & 0x3ffffffu); c = x3 >> 26;
        x4 += c; h4 = (uint32_t)(x4 & 0x3ffffffu); c = x4 >> 26;
        h0 += (uint32_t)(c * 5);
        c = h0 >> 26; h0 &= 0x3ffffffu;
        h1 += (uint32_t)c;
    }
    p->h0 = h0; p->h1 = h1; p->h2 = h2; p->h3 = h3; p->h4 = h4;
}

static void poly1305_update(struct poly1305 *p, const uint8_t *data, size_t len) {
    if (p->buf_len) {
        size_t n = 16 - p->buf_len;
        if (n > len)
            n = len;
        memcpy(p->buf + p->buf_len, data, n);
        p->buf_len += n;
        data += n;
        len -= n;
        if (p->buf_len == 16) {
            poly1305_blocks(p, p->buf, 1, 0);
            p->buf_len = 0;
        }
    }
    size_t nblocks = len / 16;
    if (nblocks) {
        poly1305_blocks(p, data, nblocks, 0);
        data += nblocks * 16;
        len -= nblocks * 16;
    }
    if (len) {
        memcpy(p->buf, data, len);
        p->buf_len = len;
    }
}

static void poly1305_finish(struct poly1305 *p, uint8_t tag[16]) {
    if (p->buf_len) {
        uint8_t block[16];
        memset(block, 0, 16);
        memcpy(block, p->buf, p->buf_len);
        block[p->buf_len] = 1;
        poly1305_blocks(p, block, 1, 1);
    }

    uint32_t h0 = p->h0, h1 = p->h1, h2 = p->h2, h3 = p->h3, h4 = p->h4;
    uint32_t c = h1 >> 26; h1 &= 0x3ffffffu; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffffu; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffffu; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffffu; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffffu; h1 += c;

    uint32_t g0 = h0 + 5;
    c = g0 >> 26; g0 &= 0x3ffffffu;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffffu;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffffu;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffffu;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t mask = (g4 >> 31) - 1u;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* Pack 26-bit limbs into 32-bit words (poly1305-donna style) */
    h0 = ((h0) | (h1 << 26)) & 0xffffffffu;
    h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffffu;
    h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffffu;
    h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffffu;

    uint64_t f = (uint64_t)h0 + le32(p->s);
    h0 = (uint32_t)f;
    f = (uint64_t)h1 + le32(p->s + 4) + (f >> 32);
    h1 = (uint32_t)f;
    f = (uint64_t)h2 + le32(p->s + 8) + (f >> 32);
    h2 = (uint32_t)f;
    f = (uint64_t)h3 + le32(p->s + 12) + (f >> 32);
    h3 = (uint32_t)f;

    wrle32(tag, h0);
    wrle32(tag + 4, h1);
    wrle32(tag + 8, h2);
    wrle32(tag + 12, h3);
}

static void poly1305_pad_update(struct poly1305 *p, size_t len) {
    uint8_t zeros[16];
    size_t pad = (16 - (len % 16)) % 16;
    if (pad) {
        memset(zeros, 0, pad);
        poly1305_update(p, zeros, pad);
    }
}

static void aead_poly1305(const uint8_t poly_key[32],
                          const uint8_t *aad, size_t aad_len,
                          const uint8_t *cipher, size_t cipher_len,
                          uint8_t tag[16]) {
    struct poly1305 p;
    poly1305_init(&p, poly_key);
    if (aad_len)
        poly1305_update(&p, aad, aad_len);
    poly1305_pad_update(&p, aad_len);
    if (cipher_len)
        poly1305_update(&p, cipher, cipher_len);
    poly1305_pad_update(&p, cipher_len);
    uint8_t lens[16];
    memset(lens, 0, 16);
    wrle32(lens, (uint32_t)aad_len);
    wrle32(lens + 4, (uint32_t)(aad_len >> 32));
    wrle32(lens + 8, (uint32_t)cipher_len);
    wrle32(lens + 12, (uint32_t)(cipher_len >> 32));
    poly1305_update(&p, lens, 16);
    poly1305_finish(&p, tag);
}

int chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plain, size_t plain_len,
                              uint8_t *cipher, uint8_t tag[16]) {
    uint8_t otk_block[64];
    chacha20_block(key, nonce, 0, otk_block);
    uint8_t poly_key[32];
    memcpy(poly_key, otk_block, 32);
    chacha20_xor(key, nonce, 1, plain, plain_len, cipher);
    aead_poly1305(poly_key, aad, aad_len, cipher, plain_len, tag);
    return 0;
}

int chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *cipher, size_t cipher_len,
                              const uint8_t tag[16], uint8_t *plain) {
    uint8_t otk_block[64];
    chacha20_block(key, nonce, 0, otk_block);
    uint8_t poly_key[32];
    memcpy(poly_key, otk_block, 32);
    uint8_t expect[16];
    aead_poly1305(poly_key, aad, aad_len, cipher, cipher_len, expect);
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++)
        diff |= expect[i] ^ tag[i];
    if (diff)
        return -1;
    chacha20_xor(key, nonce, 1, cipher, cipher_len, plain);
    return 0;
}
