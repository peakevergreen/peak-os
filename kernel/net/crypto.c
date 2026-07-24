/*
 * crypto.c — RNG glue only.
 * Primitives live in:
 *   crypto_hash.c   — SHA-256, HMAC, TLS-PRF
 *   crypto_aead.c   — AES-128-GCM, ChaCha20-Poly1305
 *   crypto_x25519.c — X25519
 */
#include "crypto.h"
#include "peak_errno.h"
#include "random.h"
#include "util.h"

int crypto_random(uint8_t *buf, size_t len) {
    /* Domain-separated ChaCha20 DRBG — see kernel/random.c / docs/csprng.md */
    if (!buf || !len)
        return PEAK_EINVAL;
    if (random_get(RANDOM_DOMAIN_CRYPTO, buf, len) != 0) {
        memzero_explicit(buf, len);
        return PEAK_EIO;
    }
    return 0;
}
