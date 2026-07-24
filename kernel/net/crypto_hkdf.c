/*
 * HKDF (RFC 5869) + TLS 1.3 HKDF-Expand-Label / Derive-Secret (RFC 8446).
 */
#include "crypto.h"
#include "util.h"

void hkdf_extract_sha256(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                         uint8_t prk[32]) {
    static const uint8_t zeros[32];
    if (!salt || salt_len == 0)
        hmac_sha256(zeros, 32, ikm, ikm_len, prk);
    else
        hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
}

void hkdf_extract_sha384(const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len,
                         uint8_t prk[48]) {
    static const uint8_t zeros[48];
    if (!salt || salt_len == 0)
        hmac_sha384(zeros, 48, ikm, ikm_len, prk);
    else
        hmac_sha384(salt, salt_len, ikm, ikm_len, prk);
}

void hkdf_expand_sha256(const uint8_t prk[32], const uint8_t *info, size_t info_len, uint8_t *out,
                        size_t out_len) {
    uint8_t t[32];
    size_t tlen = 0;
    size_t done = 0;
    uint8_t counter = 1;
    while (done < out_len) {
        uint8_t msg[32 + 256 + 1];
        size_t mlen = 0;
        if (tlen) {
            memcpy(msg, t, tlen);
            mlen = tlen;
        }
        if (info_len > 256)
            info_len = 256;
        if (info && info_len) {
            memcpy(msg + mlen, info, info_len);
            mlen += info_len;
        }
        msg[mlen++] = counter++;
        hmac_sha256(prk, 32, msg, mlen, t);
        tlen = 32;
        size_t take = out_len - done;
        if (take > 32)
            take = 32;
        memcpy(out + done, t, take);
        done += take;
    }
}

void hkdf_expand_sha384(const uint8_t prk[48], const uint8_t *info, size_t info_len, uint8_t *out,
                        size_t out_len) {
    uint8_t t[48];
    size_t tlen = 0;
    size_t done = 0;
    uint8_t counter = 1;
    while (done < out_len) {
        uint8_t msg[48 + 256 + 1];
        size_t mlen = 0;
        if (tlen) {
            memcpy(msg, t, tlen);
            mlen = tlen;
        }
        if (info_len > 256)
            info_len = 256;
        if (info && info_len) {
            memcpy(msg + mlen, info, info_len);
            mlen += info_len;
        }
        msg[mlen++] = counter++;
        hmac_sha384(prk, 48, msg, mlen, t);
        tlen = 48;
        size_t take = out_len - done;
        if (take > 48)
            take = 48;
        memcpy(out + done, t, take);
        done += take;
    }
}

/* HkdfLabel = length(2) || "tls13 " + label || context */
static int tls13_expand_label(int sha384, const uint8_t *secret, size_t secret_len, const char *label,
                              const uint8_t *context, size_t context_len, uint8_t *out,
                              size_t out_len) {
    uint8_t hkdf_label[2 + 1 + 64 + 1 + 64];
    size_t labellen = strlen(label);
    if (labellen > 58 || context_len > 64 || out_len > 255)
        return -1;
    size_t o = 0;
    hkdf_label[o++] = (uint8_t)(out_len >> 8);
    hkdf_label[o++] = (uint8_t)out_len;
    hkdf_label[o++] = (uint8_t)(6 + labellen);
    memcpy(hkdf_label + o, "tls13 ", 6);
    o += 6;
    memcpy(hkdf_label + o, label, labellen);
    o += labellen;
    hkdf_label[o++] = (uint8_t)context_len;
    if (context_len)
        memcpy(hkdf_label + o, context, context_len);
    o += context_len;
    if (sha384)
        hkdf_expand_sha384(secret, hkdf_label, o, out, out_len);
    else
        hkdf_expand_sha256(secret, hkdf_label, o, out, out_len);
    (void)secret_len;
    return 0;
}

int tls13_hkdf_expand_label(int sha384, const uint8_t *secret, size_t secret_len, const char *label,
                            const uint8_t *context, size_t context_len, uint8_t *out,
                            size_t out_len) {
    return tls13_expand_label(sha384, secret, secret_len, label, context, context_len, out,
                              out_len);
}

int tls13_derive_secret(int sha384, const uint8_t *secret, size_t secret_len, const char *label,
                        const uint8_t *transcript_hash, size_t hash_len, uint8_t *out,
                        size_t out_len) {
    return tls13_expand_label(sha384, secret, secret_len, label, transcript_hash, hash_len, out,
                              out_len);
}
