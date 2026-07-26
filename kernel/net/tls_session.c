/*
 * Bounded TLS session ticket cache (TLS 1.2 NewSessionTicket / future PSK).
 * Lookup by SNI; LRU eviction over TLS_SESSION_SLOTS.
 *
 * Hygiene: tls_session_clear and every tls_session_put scrub ticket bytes with
 * memzero_explicit before reuse so evicted PSK material does not linger.
 */
#include "tls_session.h"
#include "random.h"
#include "util.h"

struct tls_session_entry {
    char sni[TLS_SESSION_SNI_MAX];
    uint8_t ticket[TLS_SESSION_TICKET_MAX];
    uint16_t ticket_len;
    uint32_t stamp;
    uint16_t cipher;
    uint8_t tls13;
    uint8_t used;
};

static struct tls_session_entry slots[TLS_SESSION_SLOTS];
static uint32_t stamp_seq;

static void copy_sni(char *dst, const char *sni) {
    size_t i = 0;
    if (!sni)
        sni = "";
    for (; sni[i] && i + 1 < TLS_SESSION_SNI_MAX; i++) {
        char c = sni[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
    }
    dst[i] = '\0';
}

static int sni_eq(const char *a, const char *b) {
    size_t i = 0;
    for (;; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        if (!ca)
            return 1;
    }
}

void tls_session_clear(void) {
    for (int i = 0; i < TLS_SESSION_SLOTS; i++) {
        memzero_explicit(slots[i].ticket, sizeof(slots[i].ticket));
        slots[i].used = 0;
        slots[i].ticket_len = 0;
        slots[i].sni[0] = '\0';
    }
    stamp_seq = 0;
}

int tls_session_put(const char *sni, const uint8_t *ticket, size_t ticket_len,
                    const struct tls_session_meta *meta) {
    char key[TLS_SESSION_SNI_MAX];
    if (!sni || !sni[0] || !ticket || ticket_len == 0 || ticket_len > TLS_SESSION_TICKET_MAX)
        return -1;
    copy_sni(key, sni);

    int slot = -1;
    for (int i = 0; i < TLS_SESSION_SLOTS; i++) {
        if (slots[i].used && sni_eq(slots[i].sni, key)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < TLS_SESSION_SLOTS; i++) {
            if (!slots[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        uint32_t oldest = 0xffffffffu;
        for (int i = 0; i < TLS_SESSION_SLOTS; i++) {
            if (slots[i].stamp < oldest) {
                oldest = slots[i].stamp;
                slot = i;
            }
        }
    }
    if (slot < 0)
        return -1;

    /* Scrub evicted / replaced ticket before storing new material. */
    memzero_explicit(slots[slot].ticket, sizeof(slots[slot].ticket));
    copy_sni(slots[slot].sni, key);
    memcpy(slots[slot].ticket, ticket, ticket_len);
    slots[slot].ticket_len = (uint16_t)ticket_len;
    slots[slot].stamp = ++stamp_seq;
    slots[slot].cipher = meta ? meta->cipher : 0;
    slots[slot].tls13 = meta ? meta->tls13 : 0;
    slots[slot].used = 1;
    return 0;
}

int tls_session_get(const char *sni, uint8_t *ticket_out, size_t *ticket_len_inout,
                    struct tls_session_meta *meta_out) {
    char key[TLS_SESSION_SNI_MAX];
    if (!sni || !sni[0] || !ticket_out || !ticket_len_inout)
        return 0;
    copy_sni(key, sni);
    for (int i = 0; i < TLS_SESSION_SLOTS; i++) {
        if (!slots[i].used || !sni_eq(slots[i].sni, key))
            continue;
        if (*ticket_len_inout < slots[i].ticket_len)
            return 0;
        memcpy(ticket_out, slots[i].ticket, slots[i].ticket_len);
        *ticket_len_inout = slots[i].ticket_len;
        if (meta_out) {
            meta_out->cipher = slots[i].cipher;
            meta_out->tls13 = slots[i].tls13;
        }
        slots[i].stamp = ++stamp_seq; /* LRU touch */
        return 1;
    }
    return 0;
}

int tls_session_used_count(void) {
    int n = 0;
    for (int i = 0; i < TLS_SESSION_SLOTS; i++)
        if (slots[i].used)
            n++;
    return n;
}

int tls_session_max_slots(void) {
    return TLS_SESSION_SLOTS;
}

int tls_session_entry_info(int idx, char *sni_out, size_t sni_cap,
                           struct tls_session_meta *meta_out, size_t *ticket_len_out) {
    if (idx < 0 || !sni_out || sni_cap == 0)
        return -1;
    int seen = 0;
    for (int i = 0; i < TLS_SESSION_SLOTS; i++) {
        if (!slots[i].used)
            continue;
        if (seen++ != idx)
            continue;
        size_t j = 0;
        for (; slots[i].sni[j] && j + 1 < sni_cap; j++)
            sni_out[j] = slots[i].sni[j];
        sni_out[j] = '\0';
        if (meta_out) {
            meta_out->cipher = slots[i].cipher;
            meta_out->tls13 = slots[i].tls13;
        }
        if (ticket_len_out)
            *ticket_len_out = slots[i].ticket_len;
        return 0;
    }
    return -1;
}

#ifdef PEAK_HOST_TEST
int tls_session_slot_count(void) {
    return tls_session_used_count();
}
#endif
