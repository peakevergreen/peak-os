#include "crypto.h"
#include "random.h"
#include "util.h"

int crypto_random(uint8_t *buf, size_t len) {
    /* Domain-separated ChaCha20 DRBG — see kernel/random.c / docs/csprng.md */
    if (!buf || !len)
        return -1;
    if (random_get(RANDOM_DOMAIN_CRYPTO, buf, len) != 0) {
        memzero_explicit(buf, len);
        return -1;
    }
    return 0;
}
