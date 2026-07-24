#ifndef PEAK_CRYPTO_H
#define PEAK_CRYPTO_H

#include "types.h"

void sha256(const uint8_t *data, size_t len, uint8_t out[32]);
void sha384(const uint8_t *data, size_t len, uint8_t out[48]);

void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                 uint8_t out[32]);
void hmac_sha384(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len,
                 uint8_t out[48]);

/* PBKDF2-HMAC-SHA256 (RFC 8018). Writes dk_len bytes (typically 32). */
int pbkdf2_hmac_sha256(const uint8_t *pass, size_t pass_len, const uint8_t *salt,
                       size_t salt_len, uint32_t iterations, uint8_t *dk, size_t dk_len);

/* TLS 1.2 PRF with SHA-256 / SHA-384 */
void tls_prf_sha256(const uint8_t *secret, size_t secret_len, const char *label,
                    const uint8_t *seed, size_t seed_len, uint8_t *out, size_t out_len);
void tls_prf_sha384(const uint8_t *secret, size_t secret_len, const char *label,
                    const uint8_t *seed, size_t seed_len, uint8_t *out, size_t out_len);

void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void aes256_encrypt_block(const uint8_t key[32], const uint8_t in[16], uint8_t out[16]);

int aes128_gcm_encrypt(const uint8_t key[16], const uint8_t iv[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *plain, size_t plain_len,
                       uint8_t *cipher, uint8_t tag[16]);
int aes128_gcm_decrypt(const uint8_t key[16], const uint8_t iv[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *cipher, size_t cipher_len,
                       const uint8_t tag[16], uint8_t *plain);

int aes256_gcm_encrypt(const uint8_t key[32], const uint8_t iv[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *plain, size_t plain_len,
                       uint8_t *cipher, uint8_t tag[16]);
int aes256_gcm_decrypt(const uint8_t key[32], const uint8_t iv[12],
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t *cipher, size_t cipher_len,
                       const uint8_t tag[16], uint8_t *plain);

/* TLS 1.2 ChaCha20-Poly1305 (RFC 7905 / 7539) */
int chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *plain, size_t plain_len,
                              uint8_t *cipher, uint8_t tag[16]);
int chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                              const uint8_t *aad, size_t aad_len,
                              const uint8_t *cipher, size_t cipher_len,
                              const uint8_t tag[16], uint8_t *plain);

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32]);
void x25519_base(uint8_t out[32], const uint8_t scalar[32]);

/* Fails closed (-1) when crypto RNG not ready. */
int crypto_random(uint8_t *buf, size_t len);

/* Incremental SHA-256 for handshake transcript */
struct sha256_ctx {
    uint32_t h[8];
    uint8_t partial[64];
    size_t partial_len;
    uint64_t bitlen;
};
void sha256_ctx_init(struct sha256_ctx *c);
void sha256_ctx_update(struct sha256_ctx *c, const uint8_t *data, size_t len);
void sha256_ctx_final(struct sha256_ctx *c, uint8_t out[32]);

/* Incremental SHA-384 (SHA-512 truncated) for TLS 1.2 SHA384 suites */
struct sha384_ctx {
    uint64_t h[8];
    uint8_t partial[128];
    size_t partial_len;
    uint64_t bitlen_hi;
    uint64_t bitlen_lo;
};
void sha384_ctx_init(struct sha384_ctx *c);
void sha384_ctx_update(struct sha384_ctx *c, const uint8_t *data, size_t len);
void sha384_ctx_final(struct sha384_ctx *c, uint8_t out[48]);

#endif
