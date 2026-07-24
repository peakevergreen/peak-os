/*
 * Host tests for TLS trust helpers (tls_util.c), tls_trust fail-closed paths,
 * and release crypto RNG fail-closed.
 */
#include "types.h"
#include "peak_boot.h"
#include "random.h"
#include "crypto.h"
#include "tls.h"
#include "../../kernel/include/tls_util.h"
#include "../../kernel/net/tls_internal.h"

#include <stdio.h>
#include <string.h>

void tls_host_reset_trust(void);
void tls_host_seed_tofu(const char *store);
const char *tls_host_tofu_store(void);

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

uint64_t timer_ticks(void) { return 100; }
void serial_write_str(const char *s) { (void)s; }

/* Minimal Certificate handshake message (no parseable X.509 names). */
static size_t build_cert_msg(uint8_t *out, size_t cap, uint8_t fill, size_t leaf_len) {
    size_t body = 3 + 3 + leaf_len; /* list_len + cert_len + leaf */
    size_t total = 4 + body;
    if (total > cap || leaf_len > 200)
        return 0;
    out[0] = HS_CERTIFICATE;
    out[1] = (uint8_t)((body >> 16) & 0xFF);
    out[2] = (uint8_t)((body >> 8) & 0xFF);
    out[3] = (uint8_t)(body & 0xFF);
    size_t list_len = 3 + leaf_len;
    out[4] = (uint8_t)((list_len >> 16) & 0xFF);
    out[5] = (uint8_t)((list_len >> 8) & 0xFF);
    out[6] = (uint8_t)(list_len & 0xFF);
    out[7] = (uint8_t)((leaf_len >> 16) & 0xFF);
    out[8] = (uint8_t)((leaf_len >> 8) & 0xFF);
    out[9] = (uint8_t)(leaf_len & 0xFF);
    memset(out + 10, fill, leaf_len);
    return total;
}

static void test_util_helpers(void) {
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
    expect(!tls_hostname_matches_sni(NULL, "example.com"), "null pattern");
    expect(!tls_hostname_matches_sni("example.com", ""), "empty host");

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
    expect(tls_tofu_check_store(NULL, "example.com", hex64_a) == 0, "tofu null store");
    expect(tls_tofu_check_store(store, "", hex64_a) == 0, "tofu empty host");
}

static void test_pin_and_tofu_fail_closed(void) {
    uint8_t cert_a[128], cert_b[128];
    size_t len_a = build_cert_msg(cert_a, sizeof(cert_a), 0x11, 16);
    size_t len_b = build_cert_msg(cert_b, sizeof(cert_b), 0x22, 16);
    expect(len_a > 0 && len_b > 0, "cert fixtures built");

    uint8_t dig_a[32], dig_b[32], dig_bad[32];
    sha256(cert_a, len_a, dig_a);
    sha256(cert_b, len_b, dig_b);
    memset(dig_bad, 0xFF, sizeof(dig_bad));
    char hex_a[65], hex_b[65];
    tls_hex_encode(dig_a, 32, hex_a);
    tls_hex_encode(dig_b, 32, hex_b);

    /* Good pin: digest match trusts without TOFU. */
    tls_host_reset_trust();
    expect(tls_trust_pin_sha256(dig_a) == 0, "pin register");
    expect(tls_verify_cert_chain(cert_a, len_a, "pin.example") == 1, "good pin accepts");
    expect(hostname_parse_skipped == 1, "no names → parse skipped");
    expect(tls_host_tofu_store()[0] == '\0', "pin path skips tofu write");

    /* Bad pin alone does not grant trust; empty TOFU falls through to first contact. */
    tls_host_reset_trust();
    expect(tls_trust_pin_sha256(dig_bad) == 0, "bad pin register");
    expect(tls_verify_cert_chain(cert_a, len_a, "first.example") == 1, "bad pin → tofu first contact");
    expect(strstr(tls_host_tofu_store(), "first.example:") != NULL, "tofu remembered after pin miss");

    /* Bad pin + TOFU conflict → fail closed. */
    tls_host_reset_trust();
    expect(tls_trust_pin_sha256(dig_bad) == 0, "bad pin for conflict");
    char conflict[160];
    snprintf(conflict, sizeof(conflict), "known.example:%s\n", hex_b);
    tls_host_seed_tofu(conflict);
    expect(tls_verify_cert_chain(cert_a, len_a, "known.example") == 0, "bad pin + tofu conflict rejects");
    expect(cert_fail_reason != NULL, "conflict sets fail reason");
    expect(strstr(cert_fail_reason, "changed") != NULL, "conflict reason mentions change");

    /* TOFU conflict without pins → fail closed. */
    tls_host_reset_trust();
    snprintf(conflict, sizeof(conflict), "swap.example:%s\n", hex_a);
    tls_host_seed_tofu(conflict);
    expect(tls_verify_cert_chain(cert_b, len_b, "swap.example") == 0, "tofu conflict rejects");
    expect(tls_tofu_check_store(tls_host_tofu_store(), "swap.example", hex_b) == -1,
           "store still mismatches");

    /* Matching TOFU accepts. */
    tls_host_reset_trust();
    snprintf(conflict, sizeof(conflict), "ok.example:%s\n", hex_a);
    tls_host_seed_tofu(conflict);
    expect(tls_verify_cert_chain(cert_a, len_a, "ok.example") == 1, "tofu match accepts");

    /* Pin helpers reject null / overflow. */
    tls_host_reset_trust();
    expect(tls_trust_pin_sha256(NULL) != 0, "null pin rejected");
    for (int i = 0; i < TLS_PIN_MAX; i++)
        expect(tls_trust_pin_sha256(dig_a) == 0, "fill pin slots");
    expect(tls_trust_pin_sha256(dig_b) != 0, "pin overflow rejected");
}

static void test_truncated_cert_records(void) {
    uint8_t msg[64];

    tls_host_reset_trust();
    expect(tls_verify_cert_chain(NULL, 20, "h") == 0, "null cert rejects");
    expect(cert_fail_reason != NULL && strstr(cert_fail_reason, "Malformed"),
           "null cert malformed reason");

    tls_host_reset_trust();
    memset(msg, 0, sizeof(msg));
    expect(tls_verify_cert_chain(msg, 9, "h") == 0, "short buffer rejects");

    /* Pin the on-wire digest so trust succeeds, then leaf lengths are inconsistent. */
    tls_host_reset_trust();
    size_t n = build_cert_msg(msg, sizeof(msg), 0xAA, 8);
    expect(n > 0, "trunc fixture base");
    /* Inflate leaf length past end of buffer. */
    msg[7] = 0;
    msg[8] = 0;
    msg[9] = 40;
    uint8_t dig[32];
    sha256(msg, n, dig);
    expect(tls_trust_pin_sha256(dig) == 0, "pin trunc fixture");
    expect(tls_verify_cert_chain(msg, n, "trunc.example") == 0, "truncated leaf rejects");
    expect(cert_fail_reason != NULL && strstr(cert_fail_reason, "Malformed"),
           "truncated leaf malformed reason");

    /* list_len claims more bytes than remain. */
    tls_host_reset_trust();
    n = build_cert_msg(msg, sizeof(msg), 0xCC, 8);
    msg[4] = 0;
    msg[5] = 0;
    msg[6] = 50;
    sha256(msg, n, dig);
    expect(tls_trust_pin_sha256(dig) == 0, "pin list trunc fixture");
    expect(tls_verify_cert_chain(msg, n, "list.trunc") == 0, "truncated list rejects");

    tls_host_reset_trust();
    n = build_cert_msg(msg, sizeof(msg), 0xBB, 8);
    msg[0] = HS_SERVER_HELLO;
    expect(tls_verify_cert_chain(msg, n, "h") == 0, "non-certificate type rejects");
}

static void test_rng_fail_closed(void) {
    uint8_t digest[32];
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
}

/* NIST-ish empty SHA-256 + HMAC / AEAD edge vectors (fail-closed decrypt). */
static void test_crypto_edges(void) {
    /* Empty SHA-256 (FIPS 180-4). */
    uint8_t dig[32];
    sha256((const uint8_t *)"", 0, dig);
    expect(!memcmp(dig,
                   "\xe3\xb0\xc4\x42\x98\xfc\x1c\x14\x9a\xfb\xf4\xc8\x99\x6f\xb9\x24"
                   "\x27\xae\x41\xe4\x64\x9b\x93\x4c\xa4\x95\x99\x1b\x78\x52\xb8\x55",
                   32),
           "sha256 empty");

    /* Empty SHA-384 (FIPS 180-4). */
    {
        uint8_t d384[48];
        sha384((const uint8_t *)"", 0, d384);
        expect(!memcmp(d384,
                       "\x38\xb0\x60\xa7\x51\xac\x96\x38\x4c\xd9\x32\x7e\xb1\xb1\xe3\x6a"
                       "\x21\xfd\xb7\x11\x14\xbe\x07\x43\x4c\x0c\xc7\xbf\x63\xf6\xe1\xda"
                       "\x27\x4e\xde\xbf\xe7\x6f\x65\xfb\xd5\x1a\xd2\xf1\x48\x98\xb9\x5b",
                       48),
               "sha384 empty");
    }

    /* Incremental SHA-256 matches one-shot. */
    {
        const uint8_t msg[] = "abc";
        uint8_t one[32], inc[32];
        sha256(msg, 3, one);
        struct sha256_ctx c;
        sha256_ctx_init(&c);
        sha256_ctx_update(&c, msg, 1);
        sha256_ctx_update(&c, msg + 1, 2);
        sha256_ctx_final(&c, inc);
        expect(!memcmp(one, inc, 32), "sha256 incremental == one-shot");
    }

    /* HMAC-SHA256 empty key/data still produces 32 bytes (no crash). */
    {
        uint8_t mac[32];
        memset(mac, 0, sizeof(mac));
        hmac_sha256((const uint8_t *)"", 0, (const uint8_t *)"", 0, mac);
        expect(mac[0] != 0 || mac[1] != 0 || mac[31] != 0, "hmac empty non-zero");
    }

    /* AES-GCM roundtrip + tag tamper fail-closed. */
    {
        uint8_t key[16], iv[12], aad[4], plain[8], cipher[8], tag[16], out[8];
        memset(key, 0x11, sizeof(key));
        memset(iv, 0x22, sizeof(iv));
        memset(aad, 0x33, sizeof(aad));
        memcpy(plain, "peak-os!", 8);
        expect(aes128_gcm_encrypt(key, iv, aad, sizeof(aad), plain, sizeof(plain),
                                  cipher, tag) == 0,
               "gcm encrypt");
        expect(aes128_gcm_decrypt(key, iv, aad, sizeof(aad), cipher, sizeof(cipher),
                                  tag, out) == 0,
               "gcm decrypt ok");
        expect(!memcmp(out, plain, 8), "gcm roundtrip");
        tag[0] ^= 0x01;
        memset(out, 0xAA, sizeof(out));
        expect(aes128_gcm_decrypt(key, iv, aad, sizeof(aad), cipher, sizeof(cipher),
                                  tag, out) != 0,
               "gcm bad tag rejects");
        /* Empty plaintext AEAD. */
        uint8_t tag0[16];
        expect(aes128_gcm_encrypt(key, iv, aad, sizeof(aad), plain, 0, cipher, tag0) == 0,
               "gcm empty encrypt");
        expect(aes128_gcm_decrypt(key, iv, aad, sizeof(aad), cipher, 0, tag0, out) == 0,
               "gcm empty decrypt");
    }

    /* AES-256-GCM roundtrip + tag tamper. */
    {
        uint8_t key[32], iv[12], aad[4], plain[8], cipher[8], tag[16], out[8];
        memset(key, 0x44, sizeof(key));
        memset(iv, 0x55, sizeof(iv));
        memset(aad, 0x66, sizeof(aad));
        memcpy(plain, "peak256!", 8);
        expect(aes256_gcm_encrypt(key, iv, aad, sizeof(aad), plain, sizeof(plain),
                                  cipher, tag) == 0,
               "aes256 gcm encrypt");
        expect(aes256_gcm_decrypt(key, iv, aad, sizeof(aad), cipher, sizeof(cipher),
                                  tag, out) == 0,
               "aes256 gcm decrypt ok");
        expect(!memcmp(out, plain, 8), "aes256 gcm roundtrip");
        tag[0] ^= 0x01;
        expect(aes256_gcm_decrypt(key, iv, aad, sizeof(aad), cipher, sizeof(cipher),
                                  tag, out) != 0,
               "aes256 gcm bad tag");
    }

    /* ChaCha20-Poly1305 roundtrip + AAD mismatch fail-closed. */
    {
        uint8_t key[32], nonce[12], aad[3], plain[5], cipher[5], tag[16], out[5];
        memset(key, 0x44, sizeof(key));
        memset(nonce, 0x55, sizeof(nonce));
        memcpy(aad, "aad", 3);
        memcpy(plain, "hello", 5);
        expect(chacha20_poly1305_encrypt(key, nonce, aad, 3, plain, 5, cipher, tag) == 0,
               "chacha encrypt");
        expect(chacha20_poly1305_decrypt(key, nonce, aad, 3, cipher, 5, tag, out) == 0,
               "chacha decrypt");
        expect(!memcmp(out, plain, 5), "chacha roundtrip");
        uint8_t bad_aad[3] = {'A', 'A', 'D'};
        expect(chacha20_poly1305_decrypt(key, nonce, bad_aad, 3, cipher, 5, tag, out) != 0,
               "chacha aad mismatch rejects");
    }

    /* X25519 base point: clamp + base should be deterministic for fixed scalar. */
    {
        uint8_t scalar[32], out_a[32], out_b[32];
        memset(scalar, 0, sizeof(scalar));
        scalar[0] = 9;
        x25519_base(out_a, scalar);
        x25519_base(out_b, scalar);
        expect(!memcmp(out_a, out_b, 32), "x25519_base deterministic");
        expect(out_a[0] != 0 || out_a[31] != 0, "x25519_base non-zero");
    }

    /* P-256 ECDH agreement. */
    {
        struct peak_bootinfo info;
        memset(&info, 0, sizeof(info));
        info.magic = PEAK_BOOT_MAGIC;
        info.version = PEAK_BOOT_VERSION;
        info.entropy_len = 32;
        info.flags = PEAK_BOOT_FLAG_ENTROPY_OK;
        for (int i = 0; i < 32; i++)
            info.entropy[i] = (uint8_t)(0xA5 ^ i);
        random_init(&info);

        uint8_t a_priv[32], a_pub[65], b_priv[32], b_pub[65], s1[32], s2[32];
        expect(p256_keygen(a_priv, a_pub) == 0, "p256 keygen a");
        expect(p256_keygen(b_priv, b_pub) == 0, "p256 keygen b");
        expect(a_pub[0] == 0x04 && b_pub[0] == 0x04, "p256 uncompressed");
        expect(p256_ecdh(s1, a_priv, b_pub) == 0, "p256 ecdh a");
        expect(p256_ecdh(s2, b_priv, a_pub) == 0, "p256 ecdh b");
        expect(!memcmp(s1, s2, 32), "p256 shared agree");
    }

    /* TLS PRF length: request 48 bytes of key material. */
    {
        uint8_t secret[16], seed[16], out[48];
        memset(secret, 0xAB, sizeof(secret));
        memset(seed, 0xCD, sizeof(seed));
        memset(out, 0, sizeof(out));
        tls_prf_sha256(secret, sizeof(secret), "key expansion", seed, sizeof(seed),
                       out, sizeof(out));
        int nonzero = 0;
        for (size_t i = 0; i < sizeof(out); i++)
            if (out[i])
                nonzero = 1;
        expect(nonzero, "tls prf produces bytes");
        memset(out, 0, sizeof(out));
        tls_prf_sha384(secret, sizeof(secret), "key expansion", seed, sizeof(seed),
                       out, sizeof(out));
        nonzero = 0;
        for (size_t i = 0; i < sizeof(out); i++)
            if (out[i])
                nonzero = 1;
        expect(nonzero, "tls prf384 produces bytes");
    }

    /* PeakDisk PEAKDSK3 KDF: correct passphrase unwraps; wrong fails. */
    {
        const char *pass = "test-passphrase";
        uint8_t salt[16], nonce[12], tag[16], key[32], key_bad[32];
        uint8_t plain[32], cipher[32], out[32];
        memset(salt, 0x11, sizeof(salt));
        memset(nonce, 0x22, sizeof(nonce));
        memset(plain, 0x33, sizeof(plain));
        expect(pbkdf2_hmac_sha256((const uint8_t *)pass, strlen(pass), salt, 16, 1000,
                                  key, 32) == 0,
               "pbkdf2 ok");
        expect(chacha20_poly1305_encrypt(key, nonce, salt, 16, plain, sizeof(plain),
                                         cipher, tag) == 0,
               "aead encrypt");
        expect(chacha20_poly1305_decrypt(key, nonce, salt, 16, cipher, sizeof(cipher),
                                         tag, out) == 0,
               "aead decrypt good pass");
        expect(memcmp(out, plain, sizeof(plain)) == 0, "plain roundtrip");
        expect(pbkdf2_hmac_sha256((const uint8_t *)"wrong", 5, salt, 16, 1000, key_bad,
                                  32) == 0,
               "pbkdf2 wrong");
        expect(chacha20_poly1305_decrypt(key_bad, nonce, salt, 16, cipher, sizeof(cipher),
                                         tag, out) != 0,
               "wrong passphrase rejects");
    }
}

static void test_hostname_and_pin_extras(void) {
    /* Extra SNI edge cases. */
    expect(tls_hostname_matches_sni("*.a.b", "x.a.b"), "wildcard multi-label base");
    expect(!tls_hostname_matches_sni("*.a.b", "y.x.a.b"), "wildcard single label only");
    expect(!tls_hostname_matches_sni("", "example.com"), "empty pattern");

    /* Empty SNI host on verify → still need valid cert shape; pin path. */
    uint8_t cert[128];
    size_t n = build_cert_msg(cert, sizeof(cert), 0x77, 12);
    expect(n > 0, "extra cert fixture");
    uint8_t dig[32];
    sha256(cert, n, dig);
    tls_host_reset_trust();
    expect(tls_trust_pin_sha256(dig) == 0, "pin for empty sni");
    /* Empty host: pin match still verifies (TOFU skipped). */
    expect(tls_verify_cert_chain(cert, n, "") == 1, "empty sni with pin accepts");
}

static void test_ske_sig_verify(void) {
#include "rsa_ske_vectors.h"
    /* RSA PKCS#1 v1.5 and PSS accept good sigs; reject corrupt. */
    expect(rsa_verify_sha256(rsa_spki, sizeof(rsa_spki), rsa_digest, 32, rsa_sig_pkcs,
                             sizeof(rsa_sig_pkcs), 0) == 0,
           "rsa pkcs1 verify ok");
    expect(rsa_verify_sha256(rsa_spki, sizeof(rsa_spki), rsa_digest, 32, rsa_sig_pss,
                             sizeof(rsa_sig_pss), 1) == 0,
           "rsa pss verify ok");
    expect(rsa_verify_sha256(rsa_spki, sizeof(rsa_spki), rsa_digest, 32, rsa_sig_bad,
                             sizeof(rsa_sig_bad), 0) != 0,
           "rsa pkcs1 rejects bad sig");
    expect(rsa_verify_sha256(rsa_spki, sizeof(rsa_spki), rsa_digest, 32, rsa_sig_pkcs,
                             sizeof(rsa_sig_pkcs), 1) != 0,
           "rsa pss rejects pkcs padding");

    /* ECDSA P-256: sign/verify roundtrip; bit-flip rejects. */
    {
        uint8_t seed[64];
        memset(seed, 0x5a, sizeof(seed));
        random_absorb_trusted(seed, sizeof(seed));
        expect(random_ready(RANDOM_DOMAIN_CRYPTO), "crypto rng for ecdsa");

        uint8_t priv[32], pub65[65], pub[64], hash[32], sig[64], bad[64];
        memset(hash, 0x11, sizeof(hash));
        expect(p256_keygen(priv, pub65) == 0, "ecdsa keygen");
        memcpy(pub, pub65 + 1, 64);
        expect(p256_ecdsa_sign(sig, priv, hash, 32) == 0, "ecdsa sign");
        expect(p256_ecdsa_verify(sig, pub, hash, 32) == 0, "ecdsa verify ok");
        memcpy(bad, sig, 64);
        bad[0] ^= 0x01;
        expect(p256_ecdsa_verify(bad, pub, hash, 32) != 0, "ecdsa rejects bad sig");
        hash[0] ^= 0x01;
        expect(p256_ecdsa_verify(sig, pub, hash, 32) != 0, "ecdsa rejects bad hash");
    }

    /* Finished verify_data: PRF output must match expected label binding. */
    {
        uint8_t ms[48], thash[32], good[12], bad[12];
        memset(ms, 0x42, sizeof(ms));
        memset(thash, 0x7e, sizeof(thash));
        tls_prf_sha256(ms, 48, "server finished", thash, 32, good, 12);
        tls_prf_sha256(ms, 48, "client finished", thash, 32, bad, 12);
        expect(memcmp(good, bad, 12) != 0, "finished labels differ");
        uint8_t again[12];
        tls_prf_sha256(ms, 48, "server finished", thash, 32, again, 12);
        expect(!memcmp(good, again, 12), "finished verify_data stable");
    }
}

int main(void) {
    test_util_helpers();
    test_pin_and_tofu_fail_closed();
    test_truncated_cert_records();
    test_rng_fail_closed();
    test_crypto_edges();
    test_hostname_and_pin_extras();
    test_ske_sig_verify();

    if (fails) {
        fprintf(stderr, "%d failure(s)\n", fails);
        return 1;
    }
    printf("test_tls: ok\n");
    return 0;
}
