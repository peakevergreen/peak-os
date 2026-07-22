#include "crypto.h"
#include "random.h"
#include "util.h"

/* ---- SHA-256 ---- */

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void wrbe32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void wrbe64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}

static void sha256_transform(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = be32(block + i * 4);
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr(e,6)^rotr(e,11)^rotr(e,25);
        uint32_t ch = (e&f)^((~e)&g);
        uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr(a,2)^rotr(a,13)^rotr(a,22);
        uint32_t maj = (a&b)^(a&c)^(b&c);
        uint32_t t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
}

void sha256_init(uint32_t h[8]) {
    h[0]=0x6a09e667; h[1]=0xbb67ae85; h[2]=0x3c6ef372; h[3]=0xa54ff53a;
    h[4]=0x510e527f; h[5]=0x9b05688c; h[6]=0x1f83d9ab; h[7]=0x5be0cd19;
}

void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    uint32_t h[8];
    uint8_t block[64];
    size_t off = 0;
    sha256_init(h);
    while (len - off >= 64) {
        sha256_transform(h, data + off);
        off += 64;
    }
    size_t rem = len - off;
    memset(block, 0, 64);
    memcpy(block, data + off, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        sha256_transform(h, block);
        memset(block, 0, 64);
    }
    wrbe64(block + 56, (uint64_t)len * 8);
    sha256_transform(h, block);
    for (int i = 0; i < 8; i++)
        wrbe32(out + i * 4, h[i]);
}

void sha256_ctx_init(struct sha256_ctx *c) {
    sha256_init(c->h);
    c->partial_len = 0;
    c->bitlen = 0;
    memset(c->partial, 0, 64);
}

void sha256_ctx_update(struct sha256_ctx *c, const uint8_t *data, size_t len) {
    while (len > 0) {
        size_t n = 64 - c->partial_len;
        if (n > len)
            n = len;
        memcpy(c->partial + c->partial_len, data, n);
        c->partial_len += n;
        data += n;
        len -= n;
        if (c->partial_len == 64) {
            sha256_transform(c->h, c->partial);
            c->bitlen += 512;
            c->partial_len = 0;
        }
    }
}

void sha256_ctx_final(struct sha256_ctx *c, uint8_t out[32]) {
    uint64_t total_bits = c->bitlen + c->partial_len * 8;
    c->partial[c->partial_len++] = 0x80;
    if (c->partial_len > 56) {
        while (c->partial_len < 64)
            c->partial[c->partial_len++] = 0;
        sha256_transform(c->h, c->partial);
        c->partial_len = 0;
    }
    while (c->partial_len < 56)
        c->partial[c->partial_len++] = 0;
    wrbe64(c->partial + 56, total_bits);
    sha256_transform(c->h, c->partial);
    for (int i = 0; i < 8; i++)
        wrbe32(out + i * 4, c->h[i]);
}

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                 uint8_t out[32]) {
    uint8_t k[64];
    memset(k, 0, 64);
    if (key_len > 64)
        sha256(key, key_len, k);
    else
        memcpy(k, key, key_len);
    uint8_t o_key[64], i_key[64];
    for (int i = 0; i < 64; i++) {
        o_key[i] = k[i] ^ 0x5c;
        i_key[i] = k[i] ^ 0x36;
    }
    struct sha256_ctx ctx;
    uint8_t inner[32];
    sha256_ctx_init(&ctx);
    sha256_ctx_update(&ctx, i_key, 64);
    sha256_ctx_update(&ctx, data, data_len);
    sha256_ctx_final(&ctx, inner);
    sha256_ctx_init(&ctx);
    sha256_ctx_update(&ctx, o_key, 64);
    sha256_ctx_update(&ctx, inner, 32);
    sha256_ctx_final(&ctx, out);
}

void tls_prf_sha256(const uint8_t *secret, size_t secret_len, const char *label,
                    const uint8_t *seed, size_t seed_len, uint8_t *out, size_t out_len) {
    size_t label_len = strlen(label);
    uint8_t label_seed[256];
    if (label_len + seed_len > sizeof(label_seed))
        return;
    memcpy(label_seed, label, label_len);
    memcpy(label_seed + label_len, seed, seed_len);
    size_t ls_len = label_len + seed_len;

    uint8_t A[32];
    hmac_sha256(secret, secret_len, label_seed, ls_len, A);
    size_t produced = 0;
    while (produced < out_len) {
        uint8_t as[32 + 256];
        memcpy(as, A, 32);
        memcpy(as + 32, label_seed, ls_len);
        uint8_t block[32];
        hmac_sha256(secret, secret_len, as, 32 + ls_len, block);
        size_t n = out_len - produced;
        if (n > 32)
            n = 32;
        memcpy(out + produced, block, n);
        produced += n;
        hmac_sha256(secret, secret_len, A, 32, A);
    }
}

/* ---- AES-128 ---- */

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

/* ---- X25519 (RFC 7748 / TweetNaCl; limbs are int64) ---- */

typedef int64_t gf[16];

static void car25519(gf o) {
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b) {
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n) {
    gf t, m;
    for (int i = 0; i < 16; i++)
        t[i] = n[i];
    car25519(t);
    car25519(t);
    car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int64_t b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, (int)(1 - b));
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t *n) {
    for (int i = 0; i < 16; i++)
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++)
        o[i] = a[i] + b[i];
}
static void Z(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; i++)
        o[i] = a[i] - b[i];
}
static void M(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; i++)
        t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++)
        o[i] = t[i];
    car25519(o);
    car25519(o);
}
static void S(gf o, const gf a) { M(o, a, a); }

static void inv25519(gf o, const gf i) {
    gf c;
    for (int a = 0; a < 16; a++)
        c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        S(c, c);
        if (a != 2 && a != 4)
            M(c, c, i);
    }
    for (int a = 0; a < 16; a++)
        o[a] = c[a];
}

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t z[32];
    memcpy(z, scalar, 32);
    z[0] &= 248;
    z[31] &= 127;
    z[31] |= 64;
    gf a, b, c, d, e, f, x;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) {
        b[i] = x[i];
        d[i] = a[i] = c[i] = 0;
    }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; i--) {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        A(e, a, c);
        Z(a, a, c);
        A(c, b, d);
        Z(b, b, d);
        S(d, e);
        S(f, a);
        M(a, c, a);
        M(c, b, e);
        A(e, a, c);
        Z(a, a, c);
        S(b, a);
        Z(c, d, f);
        static const gf _121665 = {0xDB41, 1};
        M(a, c, _121665);
        A(a, a, d);
        M(c, c, a);
        M(a, d, f);
        M(d, b, x);
        S(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    inv25519(c, c);
    M(a, a, c);
    pack25519(out, a);
}

void x25519_base(uint8_t out[32], const uint8_t scalar[32]) {
    static const uint8_t basepoint[32] = {9};
    x25519(out, scalar, basepoint);
}

int crypto_random(uint8_t *buf, size_t len) {
    /* Domain-separated ChaCha20 DRBG — see kernel/random.c / docs/csprng.md */
    if (!buf || !len)
        return -1;
    if (random_get(RANDOM_DOMAIN_CRYPTO, buf, len) != 0) {
        memzero_explicit(buf, len);
        return -1;
    }
    return 0;
}
