/*
 * Host tests for TLS trust helpers (tls_util.c) and release fail-closed RNG.
 */
#include "types.h"
#include "peak_boot.h"
#include "random.h"
#include "crypto.h"
#include "../../kernel/include/tls_util.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

uint64_t timer_ticks(void) { return 100; }
void serial_write_str(const char *s) { (void)s; }

int main(void) {
    char hex[65];
    uint8_t digest[32];
    memset(digest, 0x42, sizeof(digest));
    tls_hex_encode(digest, 32, hex);
    expect(strlen(hex) == 64, "hex length");
    expect(!strcmp(hex,
                     "42424242424242424242424242424242"
                     "42424242424242424242424242424242"),
           "hex encode");

    expect(tls_hostname_matches_sni("example.com", "example.com"), "exact host");
    expect(tls_hostname_matches_sni("Example.COM", "example.com"), "ci host");
    expect(tls_hostname_matches_sni("*.example.com", "foo.example.com"), "wildcard");
    expect(!tls_hostname_matches_sni("*.example.com", "foo.bar.com"), "wildcard miss");
    expect(!tls_hostname_matches_sni("*.example.com", "example.com"), "wildcard needs label");

    const char *hex64_a =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    const char *hex64_b =
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    const char *hex64_c =
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
    const char *hex64_d =
        "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
    char store[256];
    snprintf(store, sizeof(store), "example.com:%s\nother.test:%s\n", hex64_a, hex64_b);
    expect(tls_tofu_check_store(store, "example.com", hex64_a) == 1, "tofu match");
    expect(tls_tofu_check_store(store, "example.com", hex64_c) == -1, "tofu mismatch");
    expect(tls_tofu_check_store(store, "unknown.test", hex64_d) == 0, "tofu unknown");

    /* Release build: weak boot entropy alone must not unlock crypto. */
    struct peak_bootinfo info;
    memset(&info, 0, sizeof(info));
    info.magic = PEAK_BOOT_MAGIC;
    info.version = PEAK_BOOT_VERSION;
    info.flags = PEAK_BOOT_FLAG_ENTROPY_WEAK;
    info.entropy_len = 32;
    for (int i = 0; i < 32; i++)
        info.entropy[i] = (uint8_t)i;
    random_init(&info);
    if (random_ready(RANDOM_DOMAIN_CRYPTO)) {
        /* Host CPU HW seed may still promote crypto; force degraded state. */
        random_host_test_clear_crypto_ready();
    }
    expect(!random_ready(RANDOM_DOMAIN_CRYPTO), "release crypto not ready");
    memset(digest, 0xEE, sizeof(digest));
    expect(crypto_random(digest, sizeof(digest)) != 0, "release crypto_random fail-closed");
    expect(digest[0] == 0 && digest[31] == 0, "release crypto_random scrub");

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("test_tls: ok\n");
    return 0;
}
