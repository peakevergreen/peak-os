#ifndef PEAK_TLS_SESSION_H
#define PEAK_TLS_SESSION_H

#include "types.h"

#define TLS_SESSION_SLOTS      4
#define TLS_SESSION_TICKET_MAX 256
#define TLS_SESSION_SNI_MAX    64
#define TLS_SESSION_NONCE_MAX  32

struct tls_session_meta {
    uint16_t cipher;
    uint8_t tls13;
    uint8_t sha384;
    uint8_t res_master_len;
    uint8_t res_master[48];
    uint8_t ticket_nonce_len;
    uint8_t ticket_nonce[TLS_SESSION_NONCE_MAX];
};

int tls_session_put(const char *sni, const uint8_t *ticket, size_t ticket_len,
                    const struct tls_session_meta *meta);

int tls_session_get(const char *sni, uint8_t *ticket_out, size_t *ticket_len_inout,
                    struct tls_session_meta *meta_out);

void tls_session_clear(void);

int tls_session_used_count(void);
int tls_session_max_slots(void);

int tls_session_entry_info(int idx, char *sni_out, size_t sni_cap,
                           struct tls_session_meta *meta_out, size_t *ticket_len_out);

#ifdef PEAK_HOST_TEST
int tls_session_slot_count(void);
#endif

#endif
