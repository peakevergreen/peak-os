#ifndef PEAK_TLS_H
#define PEAK_TLS_H

#include "types.h"

/*
 * Minimal TLS 1.2/1.3 client: ECDHE (X25519) + AES-GCM / ChaCha20-Poly1305.
 * Certificate trust: pins override, then WebPKI path-build, then opt-in TOFU.
 */

/* Structured TLS error codes (tls_last_error_code); strings remain in tls_last_error. */
enum tls_err {
    TLS_E_OK = 0,
    TLS_E_TCP = 1,
    TLS_E_RNG = 2,
    TLS_E_BUFFER = 3,
    TLS_E_ALERT = 4,
    TLS_E_HANDSHAKE = 5,
    TLS_E_CERT = 6,
    TLS_E_VERIFY = 7,
    TLS_E_TIMEOUT = 8,
    TLS_E_DOS = 9,
    TLS_E_GENERIC = 10
};

int tls_connect(uint32_t ip, uint16_t port, const char *sni_host, uint32_t timeout_ticks);
int tls_send(const void *data, size_t len);
int tls_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks);
void tls_close(void);
int tls_ready(void);
int tls_cert_verified(void);
/* 1 if SNI hostname matched leaf cert (or hostname parse was skipped). */
int tls_hostname_matched(void);
const char *tls_last_error(void);
int tls_last_error_code(void);
/* Pin a SHA-256 digest of a trusted root/SPKI (32 bytes). Returns 0 on success. */
int tls_trust_pin_sha256(const uint8_t pin[32]);
/* Clear in-memory pins + PeakFS TOFU + HSTS stores. */
void tls_trust_clear_all(void);

#endif
