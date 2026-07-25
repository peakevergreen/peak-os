#ifdef PEAK_HOST_TEST
#include "../include/tls.h"
#include "../include/tls_util.h"
#include <string.h>
#else
#include "tls.h"
#include "tls_util.h"
#include "util.h"
#endif

void tls_hex_encode(const uint8_t *in, size_t n, char *out) {
    static const char hx[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hx[in[i] >> 4];
        out[i * 2 + 1] = hx[in[i] & 0xF];
    }
    out[n * 2] = '\0';
}


const char *tls_err_name(int code) {
    switch (code) {
    case TLS_E_OK: return "ok";
    case TLS_E_TCP: return "tcp";
    case TLS_E_RNG: return "rng";
    case TLS_E_BUFFER: return "buffer";
    case TLS_E_ALERT: return "alert";
    case TLS_E_HANDSHAKE: return "handshake";
    case TLS_E_CERT: return "cert";
    case TLS_E_VERIFY: return "verify";
    case TLS_E_TIMEOUT: return "timeout";
    case TLS_E_DOS: return "dos";
    case TLS_E_GENERIC: return "generic";
    default: return "unknown";
    }
}

const char *tls_alert_desc_name(uint8_t desc) {
    switch (desc) {
    case 0: return "close_notify";
    case 10: return "unexpected_message";
    case 20: return "bad_record_mac";
    case 22: return "record_overflow";
    case 40: return "handshake_failure";
    case 42: return "bad_certificate";
    case 43: return "unsupported_certificate";
    case 44: return "certificate_revoked";
    case 45: return "certificate_expired";
    case 46: return "certificate_unknown";
    case 47: return "illegal_parameter";
    case 48: return "unknown_ca";
    case 49: return "access_denied";
    case 50: return "decode_error";
    case 51: return "decrypt_error";
    case 70: return "protocol_version";
    case 71: return "insufficient_security";
    case 80: return "internal_error";
    case 90: return "user_canceled";
    case 112: return "unrecognized_name";
    default: return "unknown";
    }
}

static int ci_eq(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

int tls_hostname_matches_sni(const char *pattern, const char *host) {
    if (!pattern || !host || !host[0])
        return 0;
    if (pattern[0] == '*' && pattern[1] == '.') {
        const char *dot = strchr(host, '.');
        if (!dot)
            return 0;
        return ci_eq(pattern + 1, dot);
    }
    return ci_eq(pattern, host);
}

int tls_tofu_check_store(const char *store, const char *host, const char *hexdigest) {
    if (!host || !host[0] || !hexdigest || !store)
        return 0;
    size_t hlen = strlen(host);
    const char *p = store;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        if (linelen > hlen + 1 && !strncmp(p, host, hlen) && p[hlen] == ':') {
            if (linelen - hlen - 1 == 64 && !strncmp(p + hlen + 1, hexdigest, 64))
                return 1;
            return -1;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    return 0;
}
