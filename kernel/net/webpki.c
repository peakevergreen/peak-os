/*
 * WebPKI path building against embedded roots (Peak-authored).
 */
#include "webpki.h"
#include "x509.h"
#include "crypto.h"
#include "rtc.h"
#include "util.h"

/* Set by tls_trust.c path; we assign on expiry. */
extern const char *cert_fail_reason;

static int cert_sha256(const uint8_t *der, size_t len, uint8_t out[32]) {
    sha256(der, len, out);
    return 0;
}

static int root_match(const uint8_t *der, size_t len) {
    uint8_t dig[32];
    cert_sha256(der, len, dig);
    for (int i = 0; i < webpki_root_count; i++) {
        if (webpki_roots[i].der_len == len && !memcmp(webpki_roots[i].der, der, len))
            return 1;
        uint8_t rd[32];
        cert_sha256(webpki_roots[i].der, webpki_roots[i].der_len, rd);
        if (!memcmp(dig, rd, 32))
            return 1;
    }
    return 0;
}

/* Extract TBSCertificate bytes and signature BIT STRING from cert DER. */
static int cert_tbs_and_sig(const uint8_t *der, size_t der_len, const uint8_t **tbs, size_t *tbs_len,
                            const uint8_t **sig, size_t *sig_len, uint16_t *sig_alg_hint) {
    if (der_len < 10 || der[0] != 0x30)
        return -1;
    size_t i = 1, L;
    uint8_t b = der[i++];
    if (b < 0x80)
        L = b;
    else if (b == 0x82) {
        L = ((size_t)der[i] << 8) | der[i + 1];
        i += 2;
    } else if (b == 0x81) {
        L = der[i++];
    } else
        return -1;
    if (i + L > der_len)
        return -1;
    const uint8_t *p = der + i;
    size_t rem = L;
    if (p[0] != 0x30)
        return -1;
    size_t th = 1;
    b = p[th++];
    size_t tl;
    if (b < 0x80)
        tl = b;
    else if (b == 0x82) {
        tl = ((size_t)p[th] << 8) | p[th + 1];
        th += 2;
    } else if (b == 0x81)
        tl = p[th++];
    else
        return -1;
    *tbs = p;
    *tbs_len = th + tl; /* include SEQUENCE tag+len of TBS */
    p += *tbs_len;
    rem -= *tbs_len;
    /* signatureAlgorithm SEQUENCE */
    if (rem < 2 || p[0] != 0x30)
        return -1;
    size_t ah = 1;
    b = p[ah++];
    size_t al;
    if (b < 0x80)
        al = b;
    else if (b == 0x81)
        al = p[ah++];
    else if (b == 0x82) {
        al = ((size_t)p[ah] << 8) | p[ah + 1];
        ah += 2;
    } else
        return -1;
    /* Look for sha256WithRSAEncryption OID 1.2.840.113549.1.1.11 */
    *sig_alg_hint = 0;
    for (size_t k = 0; k + 11 <= al; k++) {
        if (p[ah + k] == 0x2a && p[ah + k + 1] == 0x86 && p[ah + k + 8] == 0x01 &&
            p[ah + k + 9] == 0x01 && p[ah + k + 10] == 0x0b)
            *sig_alg_hint = 1; /* RSA-PKCS1-SHA256 */
    }
    p += ah + al;
    rem -= ah + al;
    if (rem < 2 || p[0] != 0x03)
        return -1;
    size_t sh = 1;
    b = p[sh++];
    size_t sl;
    if (b < 0x80)
        sl = b;
    else if (b == 0x81)
        sl = p[sh++];
    else if (b == 0x82) {
        sl = ((size_t)p[sh] << 8) | p[sh + 1];
        sh += 2;
    } else
        return -1;
    if (sl < 1)
        return -1;
    *sig = p + sh + 1; /* skip unused-bits */
    *sig_len = sl - 1;
    return 0;
}

static int verify_cert_sig(const uint8_t *subject, size_t subject_len, const uint8_t *issuer_spki,
                           size_t issuer_spki_len) {
    const uint8_t *tbs, *sig;
    size_t tbs_len, sig_len;
    uint16_t hint = 0;
    if (cert_tbs_and_sig(subject, subject_len, &tbs, &tbs_len, &sig, &sig_len, &hint) != 0)
        return -1;
    uint8_t digest[32];
    sha256(tbs, tbs_len, digest);
    /* Prefer RSA-PKCS1-SHA256; fall back to ECDSA-P256. */
    if (rsa_verify_sha256(issuer_spki, issuer_spki_len, digest, 32, sig, sig_len, 0) == 0)
        return 0;
    uint8_t pub[64], raw[64];
    int found = 0;
    for (size_t i = 0; i + 65 <= issuer_spki_len; i++) {
        if (issuer_spki[i] == 0x04 && i >= 2 && issuer_spki[i - 1] == 0x00 &&
            issuer_spki[i - 2] == 0x42) {
            memcpy(pub, issuer_spki + i + 1, 64);
            found = 1;
            break;
        }
    }
    if (!found)
        return -1;
    if (sig_len < 8 || sig[0] != 0x30)
        return -1;
    size_t off = 2;
    memset(raw, 0, 64);
    for (int part = 0; part < 2; part++) {
        if (off >= sig_len || sig[off++] != 0x02 || off >= sig_len)
            return -1;
        uint8_t ilen = sig[off++];
        if (off + ilen > sig_len || ilen == 0 || ilen > 33)
            return -1;
        const uint8_t *ip = sig + off;
        size_t skip = 0;
        while (skip < ilen && ip[skip] == 0)
            skip++;
        size_t n = ilen - skip;
        if (n > 32)
            return -1;
        memcpy(raw + part * 32 + (32 - n), ip + skip, n);
        off += ilen;
    }
    return p256_ecdsa_verify(raw, pub, digest, 32) == 0 ? 0 : -1;
}

static int time_ok(const struct x509_cert *c) {
    struct rtc_time rt;
    if (rtc_read(&rt) != 0)
        return 0; /* fail closed without clock */
    struct x509_time now = {
        .year = rt.year < 100 ? (uint16_t)(2000 + rt.year) : rt.year,
        .month = rt.month,
        .day = rt.day,
        .hour = rt.hour,
        .minute = rt.min,
        .second = rt.sec,
    };
    return x509_cert_time_valid(c, &now) == 1;
}

int webpki_verify_chain(const uint8_t *const *certs, const size_t *lens, int n,
                        const char *sni_host) {
    if (!certs || !lens || n < 1 || n > 8)
        return 0;
    struct x509_cert parsed[8];
    for (int i = 0; i < n; i++) {
        if (x509_parse_der(certs[i], lens[i], &parsed[i]) != 0)
            return 0;
        if (!time_ok(&parsed[i])) {
            cert_fail_reason = "Certificate expired or not yet valid";
            return 0;
        }
    }
    if (x509_cert_hostname_match(&parsed[0], sni_host) != 1)
        return 0;
    /* Verify signatures leaf→…→last */
    for (int i = 0; i < n - 1; i++) {
        if (!parsed[i + 1].spki ||
            verify_cert_sig(certs[i], lens[i], parsed[i + 1].spki, parsed[i + 1].spki_len) != 0)
            return 0;
        if (parsed[i + 1].has_basic_constraints && !parsed[i + 1].is_ca)
            return 0;
    }
    /* Anchor: last cert is a trust anchor, or signed by one. */
    if (root_match(certs[n - 1], lens[n - 1]))
        return 1;
    for (int r = 0; r < webpki_root_count; r++) {
        struct x509_cert root;
        if (x509_parse_der(webpki_roots[r].der, webpki_roots[r].der_len, &root) != 0)
            continue;
        if (!root.spki)
            continue;
        if (verify_cert_sig(certs[n - 1], lens[n - 1], root.spki, root.spki_len) == 0)
            return 1;
    }
    return 0;
}
