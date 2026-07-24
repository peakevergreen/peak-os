/*
 * crypto_p384.c — NIST P-384 ECDSA verify for WebPKI / TLS SKE.
 * Peak wrapper around HACL* Hacl_P384 (MIT; see hacl_p384/).
 */
#include "crypto.h"
#include "util.h"

#include "Hacl_P384.h"

/* Returns 0 on success. sig is raw r||s (96 bytes), pub is X||Y (96 bytes). */
int p384_ecdsa_verify(const uint8_t sig[96], const uint8_t pub[96], const uint8_t *hash,
                      size_t hlen) {
    uint8_t m[48];
    uint8_t pub_copy[96];
    uint8_t sig_copy[96];
    if (!sig || !pub || !hash || hlen == 0)
        return -1;
    memset(m, 0, sizeof(m));
    /* SEC1: take leftmost min(hlen, 48) bytes of the digest as the integer. */
    if (hlen >= 48)
        memcpy(m, hash, 48);
    else
        memcpy(m + (48 - hlen), hash, hlen);
    memcpy(pub_copy, pub, 96);
    memcpy(sig_copy, sig, 96);
    if (!Hacl_P384_validate_public_key(pub_copy))
        return -1;
    if (!Hacl_P384_ecdsa_verif_without_hash(48, m, pub_copy, sig_copy, sig_copy + 48))
        return -1;
    return 0;
}
