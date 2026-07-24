#ifndef PEAK_RANDOM_H
#define PEAK_RANDOM_H

#include "types.h"
#include "peak_boot.h"

/* Domain-separated DRBG requests */
enum random_domain {
    RANDOM_DOMAIN_CRYPTO = 1,
    RANDOM_DOMAIN_ASLR = 2,
    RANDOM_DOMAIN_CANARY = 3,
    RANDOM_DOMAIN_ALLOC = 4,
};

#define RANDOM_READY_CRYPTO  (1u << 0)
#define RANDOM_READY_ANY     (1u << 1)
#define RANDOM_FLAG_WEAK     (1u << 2)
#define RANDOM_FLAG_HW       (1u << 3)

void random_init(const struct peak_bootinfo *info);
void random_mix(const void *data, size_t len);
void random_mix_u64(uint64_t v);
void random_mix_irq(uint64_t tsc_bits);

/* Absorb host/HW entropy and mark CRYPTO ready (clears WEAK). */
void random_absorb_trusted(const void *data, size_t len);

/* Returns 0 on success, -1 if domain not ready (crypto fails closed in release). */
int random_get(enum random_domain domain, uint8_t *buf, size_t len);
int random_ready(enum random_domain domain);
uint32_t random_status_flags(void);

/* CPU feature report (non-secret). */
struct cpu_sec_features {
    int nx;
    int rdrand;
    int rdseed;
    int smep;
    int smap;
    int umip;
    int pcid;
};
void cpu_sec_features_probe(struct cpu_sec_features *out);
void cpu_sec_features_report(void);

/* Sensitive wipe (best-effort; compiler barrier). */
void memzero_explicit(void *p, size_t n);

#ifdef PEAK_HOST_TEST
/* Force crypto domain unavailable (host tests only). */
void random_host_test_clear_crypto_ready(void);
#endif

#endif
