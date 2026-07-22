#include "random.h"
#include "crypto.h"
#include "serial.h"
#include "util.h"
#include "timer.h"

#ifndef PEAK_DEV_INSECURE_RNG
#define PEAK_DEV_INSECURE_RNG 0
#endif

static uint8_t pool[64];
static size_t pool_fill;
static uint8_t drbg_key[32];
static uint8_t drbg_nonce[12];
static uint64_t drbg_counter;
static uint64_t bytes_since_reseed;
static uint32_t status_flags;
static int inited;
static struct cpu_sec_features g_feat;

void memzero_explicit(void *p, size_t n) {
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--)
        *v++ = 0;
    __asm__ volatile ("" ::: "memory");
}

#if defined(__x86_64__)
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b,
                  uint32_t *c, uint32_t *d) {
    __asm__ volatile ("cpuid"
                      : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                      : "a"(leaf), "c"(sub));
}

static int rdrand64(uint64_t *out) {
    uint8_t ok;
    __asm__ volatile ("rdrand %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok ? 0 : -1;
}

static int rdseed64(uint64_t *out) {
    uint8_t ok;
    __asm__ volatile ("rdseed %0; setc %1" : "=r"(*out), "=qm"(ok));
    return ok ? 0 : -1;
}

static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
static int rdrand64(uint64_t *out) {
    (void)out;
    return -1;
}
static int rdseed64(uint64_t *out) {
    (void)out;
    return -1;
}
static uint64_t rdtsc(void) {
    uint64_t t;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(t));
    return t;
}
#endif

void cpu_sec_features_probe(struct cpu_sec_features *out) {
    memset(out, 0, sizeof(*out));
#if defined(__x86_64__)
    uint32_t a, b, c, d;
    cpuid(0x80000001u, 0, &a, &b, &c, &d);
    out->nx = (d >> 20) & 1;
    cpuid(1, 0, &a, &b, &c, &d);
    out->rdrand = (c >> 30) & 1;
    cpuid(7, 0, &a, &b, &c, &d);
    out->rdseed = (b >> 18) & 1;
    out->smep = (b >> 7) & 1;
    out->smap = (b >> 20) & 1;
    out->umip = (c >> 2) & 1;
    out->pcid = 0;
    cpuid(1, 0, &a, &b, &c, &d);
    out->pcid = (c >> 17) & 1;
#else
    out->nx = 1; /* aarch64 UXN/PXN */
#endif
}

void cpu_sec_features_report(void) {
    /* Probe only — boot status covers entropy; avoid serial chatter. */
    (void)g_feat;
}

static void pool_absorb(const uint8_t *data, size_t len) {
    uint8_t digest[32];
    struct sha256_ctx ctx;
    sha256_ctx_init(&ctx);
    sha256_ctx_update(&ctx, pool, sizeof(pool));
    sha256_ctx_update(&ctx, data, len);
    sha256_ctx_final(&ctx, digest);
    for (int i = 0; i < 32; i++)
        pool[i] ^= digest[i];
    for (int i = 0; i < 32; i++)
        pool[32 + i] ^= digest[i] ^ (uint8_t)(i * 17);
    if (pool_fill < sizeof(pool))
        pool_fill += len > 32 ? 32 : len;
    if (pool_fill > sizeof(pool))
        pool_fill = sizeof(pool);
    memzero_explicit(digest, sizeof(digest));
}

void random_mix(const void *data, size_t len) {
    if (!data || !len)
        return;
    pool_absorb((const uint8_t *)data, len);
}

void random_mix_u64(uint64_t v) {
    random_mix(&v, sizeof(v));
}

void random_mix_irq(uint64_t tsc_bits) {
    uint64_t x = tsc_bits ^ (timer_ticks() << 1) ^ (uint64_t)(uintptr_t)&x;
    random_mix_u64(x);
}

/* Compact ChaCha20 block (RFC 7539) for DRBG output. */
static uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static void wrle32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void quarter(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b;
    *d ^= *a;
    *d = rotl32(*d, 16);
    *c += *d;
    *b ^= *c;
    *b = rotl32(*b, 12);
    *a += *b;
    *d ^= *a;
    *d = rotl32(*d, 8);
    *c += *d;
    *b ^= *c;
    *b = rotl32(*b, 7);
}

static void chacha20_block_raw(const uint8_t key[32], const uint8_t nonce[12],
                               uint32_t counter, uint8_t out[64]) {
    uint32_t s[16], w[16];
    s[0] = 0x61707865;
    s[1] = 0x3320646e;
    s[2] = 0x79622d32;
    s[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        s[4 + i] = le32(key + i * 4);
    s[12] = counter;
    s[13] = le32(nonce);
    s[14] = le32(nonce + 4);
    s[15] = le32(nonce + 8);
    for (int i = 0; i < 16; i++)
        w[i] = s[i];
    for (int i = 0; i < 10; i++) {
        quarter(&w[0], &w[4], &w[8], &w[12]);
        quarter(&w[1], &w[5], &w[9], &w[13]);
        quarter(&w[2], &w[6], &w[10], &w[14]);
        quarter(&w[3], &w[7], &w[11], &w[15]);
        quarter(&w[0], &w[5], &w[10], &w[15]);
        quarter(&w[1], &w[6], &w[11], &w[12]);
        quarter(&w[2], &w[7], &w[8], &w[13]);
        quarter(&w[3], &w[4], &w[9], &w[14]);
    }
    for (int i = 0; i < 16; i++)
        wrle32(out + i * 4, w[i] + s[i]);
}

static void drbg_reseed(void) {
    uint8_t material[64];
    sha256(pool, sizeof(pool), material);
    sha256(material, 32, material + 32);
    memcpy(drbg_key, material, 32);
    memcpy(drbg_nonce, material + 32, 12);
    drbg_counter = 1;
    bytes_since_reseed = 0;
    memzero_explicit(material, sizeof(material));
    /* Forward secrecy: fold key back into pool then advance. */
    pool_absorb(drbg_key, 32);
}

static int hw_seed_collect(uint8_t *buf, size_t len) {
    size_t off = 0;
    int got = 0;
    if (g_feat.rdseed) {
        while (off + 8 <= len) {
            uint64_t v;
            if (rdseed64(&v) != 0)
                break;
            memcpy(buf + off, &v, 8);
            off += 8;
            got = 1;
        }
    }
    if (g_feat.rdrand && off < len) {
        uint64_t prev = 0;
        int reps = 0;
        while (off + 8 <= len) {
            uint64_t v;
            if (rdrand64(&v) != 0)
                break;
            if (got && v == prev) {
                if (++reps > 3)
                    break; /* continuous health fail */
            } else {
                reps = 0;
            }
            prev = v;
            memcpy(buf + off, &v, 8);
            off += 8;
            got = 1;
        }
    }
    return got ? (int)off : -1;
}

void random_init(const struct peak_bootinfo *info) {
    memset(pool, 0, sizeof(pool));
    pool_fill = 0;
    status_flags = 0;
    cpu_sec_features_probe(&g_feat);
    cpu_sec_features_report();

    int trusted = 0;
    if (info) {
        if (info->entropy_len > 0 && info->entropy_len <= sizeof(info->entropy)) {
            pool_absorb(info->entropy, info->entropy_len);
            if (info->flags & PEAK_BOOT_FLAG_ENTROPY_OK)
                trusted = 1;
            if (info->flags & PEAK_BOOT_FLAG_ENTROPY_WEAK)
                status_flags |= RANDOM_FLAG_WEAK;
        }
    }

    uint8_t hw[64];
    int n = hw_seed_collect(hw, sizeof(hw));
    if (n > 0) {
        pool_absorb(hw, (size_t)n);
        status_flags |= RANDOM_FLAG_HW;
        trusted = 1;
        memzero_explicit(hw, sizeof(hw));
    }

    /* Supplemental (never sufficient alone). */
    uint64_t t = rdtsc();
    pool_absorb((uint8_t *)&t, sizeof(t));
    t = timer_ticks();
    pool_absorb((uint8_t *)&t, sizeof(t));
    if (info) {
        pool_absorb((uint8_t *)&info->mmap_count, sizeof(info->mmap_count));
        if (info->mmap_count > 0)
            pool_absorb((uint8_t *)&info->mmap[0].base, sizeof(uint64_t));
    }

    drbg_reseed();
    inited = 1;
    status_flags |= RANDOM_READY_ANY;
    if (trusted && !(status_flags & RANDOM_FLAG_WEAK))
        status_flags |= RANDOM_READY_CRYPTO;
    else if (PEAK_DEV_INSECURE_RNG) {
        status_flags |= RANDOM_READY_CRYPTO | RANDOM_FLAG_WEAK;
        serial_write_str("rng: DEV-INSECURE — crypto allowed with weak entropy\n");
    } else {
        status_flags |= RANDOM_FLAG_WEAK;
    }
}

uint32_t random_status_flags(void) {
    return status_flags;
}

#ifdef PEAK_HOST_TEST
void random_host_test_clear_crypto_ready(void) {
    status_flags &= ~RANDOM_READY_CRYPTO;
}
#endif

int random_ready(enum random_domain domain) {
    if (!inited)
        return 0;
    if (domain == RANDOM_DOMAIN_CRYPTO)
        return (status_flags & RANDOM_READY_CRYPTO) != 0;
    return (status_flags & RANDOM_READY_ANY) != 0;
}

int random_get(enum random_domain domain, uint8_t *buf, size_t len) {
    if (!buf || !len || !inited)
        return -1;
    if (domain == RANDOM_DOMAIN_CRYPTO && !(status_flags & RANDOM_READY_CRYPTO))
        return -1;

    if (bytes_since_reseed > 1u << 20)
        drbg_reseed();

    /* Domain separation via labeled extract. */
    uint8_t label[16];
    memset(label, 0, sizeof(label));
    label[0] = (uint8_t)domain;
    label[1] = 'P';
    label[2] = 'K';
    uint8_t dkey[32];
    hmac_sha256(drbg_key, 32, label, sizeof(label), dkey);

    size_t off = 0;
    uint32_t ctr = (uint32_t)drbg_counter;
    while (off < len) {
        uint8_t block[64];
        chacha20_block_raw(dkey, drbg_nonce, ctr++, block);
        size_t n = len - off;
        if (n > 64)
            n = 64;
        memcpy(buf + off, block, n);
        off += n;
        memzero_explicit(block, sizeof(block));
    }
    drbg_counter = ctr;
    bytes_since_reseed += len;
    memzero_explicit(dkey, sizeof(dkey));
    return 0;
}
