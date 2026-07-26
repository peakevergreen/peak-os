#include "dns_util.h"

void dns_host_normalize(char *out, size_t out_len, const char *in) {
    size_t i = 0;
    if (!in || out_len == 0) {
        if (out_len)
            out[0] = '\0';
        return;
    }
    for (; in[i] && i + 1 < out_len; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        out[i] = c;
    }
    out[i] = '\0';
}

int dns_host_valid(const char *host) {
    if (!host || !host[0])
        return 0;
    size_t len = 0;
    size_t label = 0;
    for (const char *p = host; *p; p++) {
        char c = *p;
        if (c == '.') {
            if (label == 0 || label > 63)
                return 0;
            label = 0;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-') {
            if (label == 0 && c == '-')
                return 0;
            label++;
            len++;
            if (len > 253)
                return 0;
            continue;
        }
        return 0;
    }
    if (label == 0 || label > 63)
        return 0;
    if (host[len] == '-' || host[0] == '-')
        return 0;
    return 1;
}
