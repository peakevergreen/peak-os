#ifdef PEAK_HOST_TEST
#include "../include/tls_util.h"
#include <string.h>
#else
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
