/*
 * TLS 1.3 PSK / binder helpers (RFC 8446 §4.2.11).
 */
#include "tls_internal.h"
#include "crypto.h"
#include "util.h"
#include "random.h"

static void hash_msg(int use_sha384, const uint8_t *msg, size_t len, uint8_t *out) {
    if (use_sha384)
        sha384(msg, len, out);
    else
        sha256(msg, len, out);
}

int tls13_compute_psk_binder(int use_sha384, const uint8_t *res_master, size_t res_len,
                             const uint8_t *ticket_nonce, size_t nonce_len,
                             const uint8_t *client_hello, size_t ch_len,
                             size_t binders_len_off, uint8_t *binder_out,
                             size_t binder_len) {
    size_t hash_len = use_sha384 ? 48u : 32u;
    if (!res_master || !res_len || !client_hello || !binder_out || binder_len != hash_len)
        return -1;
    if (binders_len_off + 2 > ch_len)
        return -1;

    uint8_t psk[48];
    if (tls13_hkdf_expand_label(use_sha384, res_master, res_len, "resumption", ticket_nonce,
                                nonce_len, psk, hash_len) != 0)
        return -1;

    uint8_t binder_key[48];
    if (tls13_hkdf_expand_label(use_sha384, psk, hash_len, "res binder", NULL, 0, binder_key,
                                hash_len) != 0) {
        memzero_explicit(psk, sizeof(psk));
        return -1;
    }

    uint8_t finished_key[48];
    if (tls13_hkdf_expand_label(use_sha384, binder_key, hash_len, "finished", NULL, 0,
                                finished_key, hash_len) != 0) {
        memzero_explicit(psk, sizeof(psk));
        memzero_explicit(binder_key, sizeof(binder_key));
        return -1;
    }

    uint8_t truncated[768];
    if (ch_len > sizeof(truncated))
        return -1;
    memcpy(truncated, client_hello, ch_len);
    truncated[binders_len_off] = 0;
    truncated[binders_len_off + 1] = 0;

    uint8_t th[48];
    hash_msg(use_sha384, truncated, ch_len, th);

    if (use_sha384)
        hmac_sha384(finished_key, hash_len, th, hash_len, binder_out);
    else
        hmac_sha256(finished_key, hash_len, th, hash_len, binder_out);

    memzero_explicit(psk, sizeof(psk));
    memzero_explicit(binder_key, sizeof(binder_key));
    memzero_explicit(finished_key, sizeof(finished_key));
    return 0;
}

/* Fallback PSK secret when only ticket bytes are cached (host tests / legacy). */
int tls13_compute_psk_binder_from_ticket(int use_sha384, const uint8_t *ticket, size_t ticket_len,
                                         const uint8_t *client_hello, size_t ch_len,
                                         size_t binders_len_off, uint8_t *binder_out,
                                         size_t binder_len) {
    size_t hash_len = use_sha384 ? 48u : 32u;
    if (!ticket || !ticket_len || !binder_out || binder_len != hash_len)
        return -1;
    uint8_t res_master[48];
    if (use_sha384)
        hkdf_extract_sha384(NULL, 0, ticket, ticket_len, res_master);
    else
        hkdf_extract_sha256(NULL, 0, ticket, ticket_len, res_master);
    return tls13_compute_psk_binder(use_sha384, res_master, hash_len, NULL, 0, client_hello, ch_len,
                                    binders_len_off, binder_out, binder_len);
}
