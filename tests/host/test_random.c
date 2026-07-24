/*
 * Host tests for Peak CSPRNG (links kernel/random.c + kernel/net/crypto.c).
 * Include Peak host types before any libc headers.
 */
#include "types.h"
#include "peak_boot.h"
#include "random.h"
#include "crypto.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static int buf_all(const uint8_t *p, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++)
        if (p[i] != v)
            return 0;
    return 1;
}

static void bootinfo_seed(struct peak_bootinfo *info, uint32_t flags, uint8_t salt) {
    memset(info, 0, sizeof(*info));
    info->magic = PEAK_BOOT_MAGIC;
    info->version = PEAK_BOOT_VERSION;
    info->flags = flags;
    info->entropy_len = 48;
    for (int i = 0; i < 48; i++)
        info->entropy[i] = (uint8_t)(salt ^ (uint8_t)i);
}

uint64_t timer_ticks(void) { return 100; }
void serial_write_str(const char *s) { (void)s; }

int main(void) {
    expect(PEAK_BOOT_VERSION == 4, "boot ABI v4");

    uint8_t a[32], b[32], c[32];
    expect(random_get(RANDOM_DOMAIN_ALLOC, a, 1) != 0, "uninit get fails");
    expect(!random_ready(RANDOM_DOMAIN_CANARY), "uninit not ready");

    struct peak_bootinfo info;
    bootinfo_seed(&info, PEAK_BOOT_FLAG_ENTROPY_OK, 0x5A);
    random_init(&info);

    expect(random_status_flags() & RANDOM_READY_ANY, "ANY ready after init");
    expect(random_ready(RANDOM_DOMAIN_ASLR), "aslr ready");
    expect(random_ready(RANDOM_DOMAIN_CANARY), "canary ready");
    expect(random_ready(RANDOM_DOMAIN_ALLOC), "alloc ready");

    if (random_ready(RANDOM_DOMAIN_CRYPTO)) {
        expect(random_status_flags() & RANDOM_READY_CRYPTO, "crypto status bit");
        expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 32) == 0, "get crypto");
        expect(random_get(RANDOM_DOMAIN_CRYPTO, b, 32) == 0, "get crypto2");
        expect(memcmp(a, b, 32) != 0, "distinct crypto blocks");
        expect(crypto_random(a, 16) == 0, "crypto_random");
    } else {
        expect(random_get(RANDOM_DOMAIN_ASLR, a, 8) == 0, "aslr when weak");
        expect(crypto_random(a, 16) != 0, "crypto fails closed when weak");
    }

    expect(random_get(RANDOM_DOMAIN_CANARY, a, 8) == 0, "canary");
    expect(random_get(RANDOM_DOMAIN_ALLOC, a, 8) == 0, "alloc");
    expect(random_get(RANDOM_DOMAIN_ASLR, b, 8) == 0, "aslr");

    /* Domain separation — labels must not produce identical streams. */
    expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 16) == 0, "domain crypto sample");
    expect(random_get(RANDOM_DOMAIN_CANARY, b, 16) == 0, "domain canary sample");
    expect(random_get(RANDOM_DOMAIN_ALLOC, c, 16) == 0, "domain alloc sample");
    expect(memcmp(a, b, 16) != 0, "crypto vs canary differ");
    expect(memcmp(b, c, 16) != 0, "canary vs alloc differ");

    expect(random_get(RANDOM_DOMAIN_CRYPTO, NULL, 8) != 0, "null buf rejected");
    expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 0) != 0, "zero len rejected");

    /* Fail-closed + scrub when crypto domain unavailable. */
    memset(a, 0xAB, sizeof(a));
    random_host_test_clear_crypto_ready();
    expect(!random_ready(RANDOM_DOMAIN_CRYPTO), "crypto cleared for test");
    expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 32) != 0, "random_get crypto fails closed");
    expect(crypto_random(a, 16) != 0, "crypto_random fails closed");
    expect(buf_all(a, 16, 0), "crypto_random scrubs output on failure");
    expect(random_ready(RANDOM_DOMAIN_CANARY), "canary still ready without crypto");
    expect(random_get(RANDOM_DOMAIN_CANARY, b, 8) == 0, "canary get after crypto cleared");

    /* Weak-entropy boot path (DEV-INSECURE build allows crypto with WEAK flag). */
    struct peak_bootinfo weak;
    bootinfo_seed(&weak, PEAK_BOOT_FLAG_ENTROPY_WEAK, 0xA5);
    random_init(&weak);
    expect(random_status_flags() & RANDOM_FLAG_WEAK, "weak entropy flagged");
    expect(random_ready(RANDOM_DOMAIN_ASLR), "aslr ready on weak boot");
#if PEAK_DEV_INSECURE_RNG
    expect(random_ready(RANDOM_DOMAIN_CRYPTO), "dev insecure crypto on weak boot");
    expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 16) == 0, "weak boot crypto get");
#else
    expect(!random_ready(RANDOM_DOMAIN_CRYPTO), "release crypto closed on weak boot");
#endif

    /* virtio-rng / host path: absorb trusted bytes unlocks CRYPTO and clears WEAK. */
    {
        uint8_t seed[32];
        for (int i = 0; i < 32; i++)
            seed[i] = (uint8_t)(0x3C ^ i);
        random_absorb_trusted(seed, sizeof(seed));
        expect(random_ready(RANDOM_DOMAIN_CRYPTO), "absorb unlocks crypto");
        expect(random_status_flags() & RANDOM_FLAG_HW, "absorb sets HW");
        expect(!(random_status_flags() & RANDOM_FLAG_WEAK), "absorb clears WEAK");
        expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 16) == 0, "crypto get after absorb");
    }

    memset(b, 0xCD, sizeof(b));
    memzero_explicit(b, sizeof(b));
    expect(buf_all(b, sizeof(b), 0), "memzero_explicit clears buffer");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("test_random: ok\n");
    return 0;
}
