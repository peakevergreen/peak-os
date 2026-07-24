#ifndef PEAK_TLS_ECH_H
#define PEAK_TLS_ECH_H

#include "types.h"

/* Encrypted Client Hello (draft-ietf-tls-esni) — Peak scaffold.
 * Full HPKE outer/inner CH is not shipped yet; when ECH is required and no
 * config/keys are present, handshake fails closed.
 */

#define TLS_ECH_CONFIG_MAX 512

/* Install ECHConfigList bytes (from HTTPS DNS or file). Returns 0 on accept. */
int tls_ech_set_config(const uint8_t *cfg, size_t len);
void tls_ech_clear_config(void);
int tls_ech_have_config(void);

/* When set, ClientHello build fails unless a config is installed. */
void tls_ech_set_required(int on);
int tls_ech_required(void);

/* 0 ok; -1 missing config while required; -2 encode/HPKE not implemented. */
int tls_ech_prepare_client_hello(void);

#endif
