/*
 * crypto_hash.c — SHA-256, MD5, HMAC-SHA256, TLS 1.2 PRF (SHA-256).
 * See also crypto_aead.c, crypto_x25519.c, crypto.c (RNG glue).
 */
#include "crypto.h"
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

/* ---- MD5 (RFC 1321) ---- */

static const uint32_t MD5_K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};
static const uint8_t MD5_S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wrle32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t rol32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
        x[i] = le32(block + i * 4);
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = (uint32_t)i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5u * (uint32_t)i + 1u) & 15u;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3u * (uint32_t)i + 5u) & 15u;
        } else {
            f = c ^ (b | (~d));
            g = (7u * (uint32_t)i) & 15u;
        }
        uint32_t t = d;
        d = c;
        c = b;
        b = b + rol32(a + f + MD5_K[i] + x[g], MD5_S[i]);
        a = t;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

void md5(const uint8_t *data, size_t len, uint8_t out[16]) {
    uint32_t state[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    uint8_t block[64];
    size_t off = 0;
    uint64_t bitlen = (uint64_t)len * 8;
    while (len - off >= 64) {
        md5_transform(state, data + off);
        off += 64;
    }
    size_t rem = len - off;
    memset(block, 0, 64);
    memcpy(block, data + off, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        md5_transform(state, block);
        memset(block, 0, 64);
    }
    wrle32(block + 56, (uint32_t)bitlen);
    wrle32(block + 60, (uint32_t)(bitlen >> 32));
    md5_transform(state, block);
    for (int i = 0; i < 4; i++)
        wrle32(out + i * 4, state[i]);
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

int pbkdf2_hmac_sha256(const uint8_t *pass, size_t pass_len, const uint8_t *salt,
                       size_t salt_len, uint32_t iterations, uint8_t *dk, size_t dk_len) {
    if (!pass || !salt || !dk || !iterations || !dk_len || salt_len > 64)
        return -1;
    size_t produced = 0;
    uint32_t block_index = 1;
    while (produced < dk_len) {
        uint8_t u[32], t[32];
        uint8_t msg[64 + 4];
        memcpy(msg, salt, salt_len);
        msg[salt_len] = (uint8_t)(block_index >> 24);
        msg[salt_len + 1] = (uint8_t)(block_index >> 16);
        msg[salt_len + 2] = (uint8_t)(block_index >> 8);
        msg[salt_len + 3] = (uint8_t)block_index;
        hmac_sha256(pass, pass_len, msg, salt_len + 4, u);
        memcpy(t, u, 32);
        for (uint32_t i = 1; i < iterations; i++) {
            hmac_sha256(pass, pass_len, u, 32, u);
            for (int j = 0; j < 32; j++)
                t[j] ^= u[j];
        }
        size_t n = dk_len - produced;
        if (n > 32)
            n = 32;
        memcpy(dk + produced, t, n);
        produced += n;
        block_index++;
    }
    return 0;
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
