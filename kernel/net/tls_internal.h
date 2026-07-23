#ifndef PEAK_TLS_INTERNAL_H
#define PEAK_TLS_INTERNAL_H

#include "tls.h"
#include "crypto.h"
#include "tls_util.h"
#include "types.h"

#define TLS_CONTENT_HS    22
#define TLS_CONTENT_CCS   20
#define TLS_CONTENT_APP   23
#define TLS_CONTENT_ALERT 21

#define HS_CLIENT_HELLO      1
#define HS_SERVER_HELLO      2
#define HS_CERTIFICATE       11
#define HS_SERVER_KEY_EX     12
#define HS_SERVER_HELLO_DONE 14
#define HS_CLIENT_KEY_EX     16
#define HS_FINISHED          20

#define CS_ECDHE_RSA_AES128_GCM   0xC02F
#define CS_ECDHE_ECDSA_AES128_GCM 0xC02B
#define CS_ECDHE_RSA_CHACHA20     0xCCA8
#define CS_ECDHE_ECDSA_CHACHA20   0xCCA9

#define CIPHER_AES128_GCM 0
#define CIPHER_CHACHA20   1

#define TLS_PIN_MAX 8
#define TOFU_PATH   "/etc/peak/tls-tofu"
#define TOFU_MAX    8192

/* Session state (defined in tls.c). */
extern int tls_up;
extern int cipher_kind;
extern int use_ems;
extern int cert_verified;
extern int hostname_matched;
extern int hostname_parse_skipped;
extern const char *cert_fail_reason;
extern char last_err[96];
extern uint8_t trust_pins[TLS_PIN_MAX][32];
extern int trust_pin_count;
extern uint8_t client_random[32];
extern uint8_t server_random[32];
extern uint8_t master_secret[48];
extern uint8_t client_key[32], server_key[32];
extern uint8_t client_iv[12], server_iv[12];
extern uint64_t client_seq, server_seq;
extern struct sha256_ctx transcript;
extern uint8_t rx_app[16384];
extern size_t rx_app_len;
extern uint8_t hs_reasm[24576];
extern size_t hs_reasm_len;

/* tls.c */
void tls_set_err(const char *msg);

/* tls_record.c */
void tls_wr16(uint8_t *p, uint16_t v);
void tls_wr24(uint8_t *p, uint32_t v);
uint16_t tls_rd16(const uint8_t *p);
uint32_t tls_rd24(const uint8_t *p);
int tls_send_record(uint8_t type, const uint8_t *data, size_t len, int encrypted);
int tls_recv_record(uint8_t *type_out, uint8_t *buf, size_t cap, size_t *out_len,
                    uint32_t timeout_ticks, int encrypted);

/* tls_trust.c */
int tls_verify_cert_chain(const uint8_t *cert_msg, size_t len, const char *sni_host);

#endif
