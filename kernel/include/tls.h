#ifndef PEAK_TLS_H
#define PEAK_TLS_H

#include "types.h"

/*
 * Minimal TLS 1.2/1.3 client: ECDHE (X25519) + AES-GCM / ChaCha20-Poly1305.
 * Certificate trust: explicit SHA-256 pins when registered, otherwise
 * trust-on-first-use per SNI host (persisted at /etc/peak/tls-tofu).
 * A changed certificate for a known host fails closed.
 */

int tls_connect(uint32_t ip, uint16_t port, const char *sni_host, uint32_t timeout_ticks);
int tls_send(const void *data, size_t len);
int tls_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks);
void tls_close(void);
int tls_ready(void);
int tls_cert_verified(void);
/* 1 if SNI hostname matched leaf cert (or hostname parse was skipped). */
int tls_hostname_matched(void);
const char *tls_last_error(void);
/* Pin a SHA-256 digest of a trusted root/SPKI (32 bytes). Returns 0 on success. */
int tls_trust_pin_sha256(const uint8_t pin[32]);

#endif
