#ifndef PEAK_TLS_SESSION_H
#define PEAK_TLS_SESSION_H

#include "types.h"

#define TLS_SESSION_SLOTS      4
#define TLS_SESSION_TICKET_MAX 256
#define TLS_SESSION_SNI_MAX    64

struct tls_session_meta {
    uint16_t cipher;
    uint8_t tls13;
};

/* Store ticket for SNI (LRU over TLS_SESSION_SLOTS). Returns 0 on success. */
int tls_session_put(const char *sni, const uint8_t *ticket, size_t ticket_len,
                    const struct tls_session_meta *meta);

/* Lookup ticket by SNI. Returns 1 hit, 0 miss. */
int tls_session_get(const char *sni, uint8_t *ticket_out, size_t *ticket_len_inout,
                    struct tls_session_meta *meta_out);

void tls_session_clear(void);

#ifdef PEAK_HOST_TEST
int tls_session_slot_count(void);
#endif

#endif
