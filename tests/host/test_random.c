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

uint64_t timer_ticks(void) { return 100; }
void serial_write_str(const char *s) { (void)s; }

int main(void) {
    expect(PEAK_BOOT_VERSION == 4, "boot ABI v4");

    struct peak_bootinfo info;
    memset(&info, 0, sizeof(info));
    info.magic = PEAK_BOOT_MAGIC;
    info.version = PEAK_BOOT_VERSION;
    info.flags = PEAK_BOOT_FLAG_ENTROPY_OK;
    info.entropy_len = 48;
    for (int i = 0; i < 48; i++)
        info.entropy[i] = (uint8_t)(0x5A ^ i);

    random_init(&info);

    uint8_t a[32], b[32];
    memset(a, 0, 32);
    memset(b, 0xff, 32);

    if (random_ready(RANDOM_DOMAIN_CRYPTO)) {
        expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 32) == 0, "get crypto");
        expect(random_get(RANDOM_DOMAIN_CRYPTO, b, 32) == 0, "get crypto2");
        expect(memcmp(a, b, 32) != 0, "distinct blocks");
        expect(crypto_random(a, 16) == 0, "crypto_random");
    } else {
        expect(random_get(RANDOM_DOMAIN_ASLR, a, 8) == 0, "aslr when weak");
        expect(crypto_random(a, 16) != 0, "crypto fails closed when weak");
    }

    expect(random_get(RANDOM_DOMAIN_CANARY, a, 8) == 0, "canary");
    expect(random_get(RANDOM_DOMAIN_ALLOC, a, 8) == 0, "alloc");

    /* Fail-closed + scrub when crypto domain unavailable. */
    memset(a, 0xAB, sizeof(a));
    random_host_test_clear_crypto_ready();
    expect(!random_ready(RANDOM_DOMAIN_CRYPTO), "crypto cleared for test");
    expect(random_get(RANDOM_DOMAIN_CRYPTO, a, 32) != 0, "random_get crypto fails closed");
    expect(crypto_random(a, 16) != 0, "crypto_random fails closed");
    expect(buf_all(a, 16, 0), "crypto_random scrubs output on failure");

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
