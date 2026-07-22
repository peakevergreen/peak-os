#ifdef PEAK_HOST_TEST
#include "../include/dhcp_util.h"
#include <string.h>
#else
#include "dhcp_util.h"
#include "util.h"
#endif

int peak_parse_ipv4(const char *s, uint32_t *out) {
    if (!s || !out)
        return -1;
    uint32_t parts[4];
    for (int i = 0; i < 4; i++) {
        uint32_t v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (uint32_t)(*s - '0');
            if (v > 255)
                return -1;
            s++;
            digits++;
        }
        if (!digits)
            return -1;
        parts[i] = v;
        if (i < 3) {
            if (*s != '.')
                return -1;
            s++;
        }
    }
    if (*s != '\0')
        return -1;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

int dhcp_parse_options(const uint8_t *opts, size_t opt_len, struct dhcp_lease *out) {
    if (!opts || !out || opt_len < 4)
        return -1;
    memset(out, 0, sizeof(*out));
    /* optional: verify magic already stripped by caller */
    size_t i = 0;
    if (opt_len >= 4 && opts[0] == 99 && opts[1] == 130 && opts[2] == 83 &&
        opts[3] == 99)
        i = 4;
    while (i < opt_len) {
        uint8_t code = opts[i++];
        if (code == 0)
            continue;
        if (code == 255)
            break;
        if (i >= opt_len)
            return -1;
        uint8_t len = opts[i++];
        if (i + len > opt_len)
            return -1;
        const uint8_t *v = opts + i;
        if (code == 53 && len == 1)
            out->msg_type = v[0];
        else if (code == 1 && len == 4)
            out->mask = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                        ((uint32_t)v[2] << 8) | v[3];
        else if (code == 3 && len >= 4)
            out->gw = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                      ((uint32_t)v[2] << 8) | v[3];
        else if (code == 6 && len >= 4)
            out->dns = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                       ((uint32_t)v[2] << 8) | v[3];
        else if (code == 54 && len == 4)
            out->server_id = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                             ((uint32_t)v[2] << 8) | v[3];
        i += len;
    }
    return out->msg_type ? 0 : -1;
}
