/*
 * crypto_sha384.c — SHA-384 (SHA-512 truncated), HMAC, TLS 1.2 PRF-SHA384.
 */
#include "crypto.h"
#include "util.h"

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL,0xefbe4786384f25e3ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL,0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL,0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL,0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL,0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,
    0x650a73548baf63deULL,0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL,0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL,0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL,0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL,0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL,0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL,0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,
    0xca273eceea26619cULL,0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL,0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,
    0x28db77f523047d84ULL,0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL,0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

static uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }
static uint64_t be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    return v;
}
static void wrbe64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}

static void sha512_transform(uint64_t h[8], const uint8_t block[128]) {
    uint64_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = be64(block + i * 8);
    for (int i = 16; i < 80; i++) {
        uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint64_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; i++) {
        uint64_t S1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t t1 = hh + S1 + ch + K512[i] + w[i];
        uint64_t S0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

static void sha384_init_state(uint64_t h[8]) {
    h[0] = 0xcbbb9d5dc1059ed8ULL;
    h[1] = 0x629a292a367cd507ULL;
    h[2] = 0x9159015a3070dd17ULL;
    h[3] = 0x152fecd8f70e5939ULL;
    h[4] = 0x67332667ffc00b31ULL;
    h[5] = 0x8eb44a8768581511ULL;
    h[6] = 0xdb0c2e0d64f98fa7ULL;
    h[7] = 0x47b5481dbefa4fa4ULL;
}

void sha384(const uint8_t *data, size_t len, uint8_t out[48]) {
    struct sha384_ctx c;
    sha384_ctx_init(&c);
    sha384_ctx_update(&c, data, len);
    sha384_ctx_final(&c, out);
}

void sha384_ctx_init(struct sha384_ctx *c) {
    sha384_init_state(c->h);
    c->partial_len = 0;
    c->bitlen_hi = 0;
    c->bitlen_lo = 0;
    memset(c->partial, 0, 128);
}

void sha384_ctx_update(struct sha384_ctx *c, const uint8_t *data, size_t len) {
    while (len > 0) {
        size_t n = 128 - c->partial_len;
        if (n > len)
            n = len;
        memcpy(c->partial + c->partial_len, data, n);
        c->partial_len += n;
        data += n;
        len -= n;
        if (c->partial_len == 128) {
            sha512_transform(c->h, c->partial);
            uint64_t add = 1024;
            c->bitlen_lo += add;
            if (c->bitlen_lo < add)
                c->bitlen_hi++;
            c->partial_len = 0;
        }
    }
}

void sha384_ctx_final(struct sha384_ctx *c, uint8_t out[48]) {
    uint64_t lo = c->bitlen_lo + (uint64_t)c->partial_len * 8;
    uint64_t hi = c->bitlen_hi;
    if (lo < (uint64_t)c->partial_len * 8)
        hi++;
    c->partial[c->partial_len++] = 0x80;
    if (c->partial_len > 112) {
        while (c->partial_len < 128)
            c->partial[c->partial_len++] = 0;
        sha512_transform(c->h, c->partial);
        c->partial_len = 0;
    }
    while (c->partial_len < 112)
        c->partial[c->partial_len++] = 0;
    wrbe64(c->partial + 112, hi);
    wrbe64(c->partial + 120, lo);
    sha512_transform(c->h, c->partial);
    for (int i = 0; i < 6; i++)
        wrbe64(out + i * 8, c->h[i]);
}

void hmac_sha384(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                 uint8_t out[48]) {
    uint8_t k[128];
    memset(k, 0, 128);
    if (key_len > 128) {
        uint8_t dig[48];
        sha384(key, key_len, dig);
        memcpy(k, dig, 48);
    } else {
        memcpy(k, key, key_len);
    }
    uint8_t o_key[128], i_key[128];
    for (int i = 0; i < 128; i++) {
        o_key[i] = k[i] ^ 0x5c;
        i_key[i] = k[i] ^ 0x36;
    }
    struct sha384_ctx ctx;
    uint8_t inner[48];
    sha384_ctx_init(&ctx);
    sha384_ctx_update(&ctx, i_key, 128);
    sha384_ctx_update(&ctx, data, data_len);
    sha384_ctx_final(&ctx, inner);
    sha384_ctx_init(&ctx);
    sha384_ctx_update(&ctx, o_key, 128);
    sha384_ctx_update(&ctx, inner, 48);
    sha384_ctx_final(&ctx, out);
}

void tls_prf_sha384(const uint8_t *secret, size_t secret_len, const char *label,
                    const uint8_t *seed, size_t seed_len, uint8_t *out, size_t out_len) {
    size_t label_len = strlen(label);
    uint8_t label_seed[256];
    if (label_len + seed_len > sizeof(label_seed))
        return;
    memcpy(label_seed, label, label_len);
    memcpy(label_seed + label_len, seed, seed_len);
    size_t ls_len = label_len + seed_len;

    uint8_t A[48];
    hmac_sha384(secret, secret_len, label_seed, ls_len, A);
    size_t produced = 0;
    while (produced < out_len) {
        uint8_t as[48 + 256];
        memcpy(as, A, 48);
        memcpy(as + 48, label_seed, ls_len);
        uint8_t block[48];
        hmac_sha384(secret, secret_len, as, 48 + ls_len, block);
        size_t n = out_len - produced;
        if (n > 48)
            n = 48;
        memcpy(out + produced, block, n);
        produced += n;
        hmac_sha384(secret, secret_len, A, 48, A);
    }
}
