#include "tls_internal.h"
#include "tls_hsts.h"
#include "serial.h"
#include "tls_util.h"
#include "util.h"
#include "vfs.h"
#include "x509.h"
#include "rtc.h"
#include "webpki.h"
#include "settings.h"
#include "random.h"

/*
 * Trust-on-first-use store: /etc/peak/tls-tofu holds "host:hex64" lines
 * mapping an SNI hostname to the SHA-256 of its Certificate message.
 * First contact records the digest; later contacts must match it.
 * Explicit pins (tls_trust_pin_sha256) always win when present.
 */

int tls_trust_pin_sha256(const uint8_t pin[32]) {
    if (!pin || trust_pin_count >= TLS_PIN_MAX)
        return -1;
    memcpy(trust_pins[trust_pin_count], pin, 32);
    trust_pin_count++;
    return 0;
}

int tls_cert_verified(void) {
    return cert_verified;
}

int tls_hostname_matched(void) {
    return hostname_matched;
}

void tls_trust_clear_all(void) {
    trust_pin_count = 0;
    memzero_explicit(trust_pins, sizeof(trust_pins));
    vfs_write_file(TOFU_PATH, "", 0);
    hsts_clear();
}

/* Shared: tofu_check then tofu_remember never nest (saves one TOFU_MAX BSS). */
static char tofu_buf[TOFU_MAX];

/* 1 = match, 0 = unknown host, -1 = MISMATCH (possible MITM). */
static int tofu_check(const char *host, const char *hexdigest) {
    size_t n = 0;
    if (!host || !host[0])
        return 0;
    if (vfs_read_file(TOFU_PATH, tofu_buf, sizeof(tofu_buf) - 1, &n) != 0 || n == 0)
        return 0;
    tofu_buf[n] = '\0';
    return tls_tofu_check_store(tofu_buf, host, hexdigest);
}

static void tofu_remember(const char *host, const char *hexdigest) {
    size_t n = 0;
    if (!host || !host[0])
        return;
    if (vfs_read_file(TOFU_PATH, tofu_buf, sizeof(tofu_buf) - 1, &n) != 0)
        n = 0;
    tofu_buf[n] = '\0';
    size_t need = strlen(host) + 1 + 64 + 1;
    if (n + need + 1 > sizeof(tofu_buf))
        return; /* store full — keep existing entries */
    size_t o = n;
    for (const char *s = host; *s; s++)
        tofu_buf[o++] = *s;
    tofu_buf[o++] = ':';
    for (const char *s = hexdigest; *s; s++)
        tofu_buf[o++] = *s;
    tofu_buf[o++] = '\n';
    vfs_write_file(TOFU_PATH, tofu_buf, o);
}

static int x509_names_match_sni(const uint8_t *cert, size_t cert_len, const char *sni_host) {
    struct x509_cert xc;
    if (x509_parse_der(cert, cert_len, &xc) == 0) {
        int m = x509_cert_hostname_match(&xc, sni_host);
        if (m >= 0)
            return m;
    }
    /* Legacy heuristic fallback if DER parse finds no SANs. */
    int found_name = 0;
    int matched = 0;
    for (size_t i = 0; i + 6 < cert_len; i++) {
        if (cert[i] == 0x55 && cert[i + 1] == 0x04 && cert[i + 2] == 0x03) {
            size_t j = i + 3;
            while (j + 2 < cert_len && cert[j] != 0x0C)
                j++;
            if (j + 2 >= cert_len || cert[j] != 0x0C)
                continue;
            j++;
            size_t vlen = cert[j++];
            if (vlen >= 128 || j + vlen > cert_len)
                continue;
            char name[128];
            memcpy(name, cert + j, vlen);
            name[vlen] = '\0';
            found_name = 1;
            if (tls_hostname_matches_sni(name, sni_host))
                matched = 1;
        }
        if (cert[i] == 0x82 && i + 2 < cert_len) {
            size_t vlen = cert[i + 1];
            if (vlen > 0 && vlen < 128 && i + 2 + vlen <= cert_len) {
                char name[128];
                memcpy(name, cert + i + 2, vlen);
                name[vlen] = '\0';
                if (strchr(name, '.') && tls_hostname_matches_sni(name, sni_host)) {
                    found_name = 1;
                    matched = 1;
                }
            }
        }
    }
    if (matched)
        return 1;
    return found_name ? 0 : -1;
}

static int leaf_cert_from_msg(const uint8_t *cert_msg, size_t len, const uint8_t **leaf,
                              size_t *leaf_len) {
    if (!cert_msg || len < 10 || cert_msg[0] != HS_CERTIFICATE)
        return -1;
    size_t off = 4;
    if (off + 3 > len)
        return -1;
    size_t list_len = ((size_t)cert_msg[off] << 16) | ((size_t)cert_msg[off + 1] << 8) |
                      cert_msg[off + 2];
    off += 3;
    if (off + 3 > len || off + list_len > len)
        return -1;
    size_t cert_len = ((size_t)cert_msg[off] << 16) | ((size_t)cert_msg[off + 1] << 8) |
                      cert_msg[off + 2];
    off += 3;
    if (off + cert_len > len)
        return -1;
    *leaf = cert_msg + off;
    *leaf_len = cert_len;
    return 0;
}

/* 1 = matched, 0 = mismatch, -1 = no usable names, -2 = truncated/malformed leaf. */
static int verify_cert_hostname(const uint8_t *cert_msg, size_t len, const char *sni_host) {
    const uint8_t *leaf;
    size_t leaf_len;
    if (leaf_cert_from_msg(cert_msg, len, &leaf, &leaf_len) != 0)
        return -2;
    return x509_names_match_sni(leaf, leaf_len, sni_host);
}

/*
 * Verify the server Certificate message.
 * Order: explicit pins → WebPKI path to embedded roots → optional TOFU
 * (settings_tls_tofu). Hostname must match. Validity enforced under WebPKI.
 */
int tls_verify_cert_chain(const uint8_t *cert_msg, size_t len, const char *sni_host) {
    cert_fail_reason = NULL;
    hostname_matched = 0;
    hostname_parse_skipped = 0;
    if (!cert_msg || len < 10 || cert_msg[0] != HS_CERTIFICATE) {
        cert_fail_reason = "Malformed Certificate message";
        return 0;
    }
    uint8_t digest[32];
    sha256(cert_msg, len, digest);

    /* 1. Explicit pins win. */
    for (int i = 0; i < trust_pin_count; i++) {
        if (!memcmp(digest, trust_pins[i], 32)) {
            int hn = verify_cert_hostname(cert_msg, len, sni_host);
            if (hn == 0) {
                cert_fail_reason = "Certificate hostname mismatch";
                return 0;
            }
            if (hn == -2) {
                cert_fail_reason = "Malformed Certificate message";
                return 0;
            }
            hostname_matched = 1;
            if (hn < 0)
                hostname_parse_skipped = 1;
            return 1;
        }
    }

    /* 2. WebPKI path build (default). */
    {
        const uint8_t *certs[8];
        size_t lens[8];
        int n = 0;
        size_t off = 4;
        if (off + 3 > len)
            goto webpki_fail;
        size_t list_len = ((size_t)cert_msg[off] << 16) | ((size_t)cert_msg[off + 1] << 8) |
                          cert_msg[off + 2];
        off += 3;
        size_t list_end = off + list_len;
        if (list_end > len)
            goto webpki_fail;
        while (off + 3 <= list_end && n < 8) {
            size_t clen = ((size_t)cert_msg[off] << 16) | ((size_t)cert_msg[off + 1] << 8) |
                          cert_msg[off + 2];
            off += 3;
            if (off + clen > list_end)
                goto webpki_fail;
            certs[n] = cert_msg + off;
            lens[n] = clen;
            n++;
            off += clen;
            /* Skip TLS 1.3 per-cert extensions if present (2-byte length). Detect by
             * remaining bytes that look like ext len after a cert — for 1.2 list there
             * are none. Heuristic: if next bytes are not a 3-byte cert len pattern inside
             * list, stop. Standard 1.2 has only certs. */
        }
        if (n > 0 && webpki_verify_chain(certs, lens, n, sni_host)) {
            hostname_matched = 1;
            serial_log(SERIAL_LOG_INFO, "tls: WebPKI path verified\n");
            return 1;
        }
    }
webpki_fail:

    /* 3. Opt-in TOFU continuity. */
    if (settings_tls_tofu()) {
        char hexd[65];
        tls_hex_encode(digest, 32, hexd);
        int t = tofu_check(sni_host, hexd);
        if (t == 1) {
            int hn = verify_cert_hostname(cert_msg, len, sni_host);
            if (hn == 0) {
                cert_fail_reason = "Certificate hostname mismatch";
                return 0;
            }
            if (hn == -2) {
                cert_fail_reason = "Malformed Certificate message";
                return 0;
            }
            hostname_matched = 1;
            if (hn < 0)
                hostname_parse_skipped = 1;
            return 1;
        }
        if (t < 0) {
            cert_fail_reason = "Cert changed for known host; rm /etc/peak/tls-tofu to re-trust";
            return 0;
        }
        tofu_remember(sni_host, hexd);
        serial_log(SERIAL_LOG_INFO, "tls: TOFU remembered (opt-in)\n");
        int hn = verify_cert_hostname(cert_msg, len, sni_host);
        if (hn == 0) {
            cert_fail_reason = "Certificate hostname mismatch";
            return 0;
        }
        if (hn == -2) {
            cert_fail_reason = "Malformed Certificate message";
            return 0;
        }
        hostname_matched = 1;
        if (hn < 0)
            hostname_parse_skipped = 1;
        return 1;
    }

    cert_fail_reason = "Untrusted certificate (WebPKI path failed)";
    serial_log(SERIAL_LOG_WARN, "tls: WebPKI verify failed\n");
    return 0;
}
