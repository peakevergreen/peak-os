#include "peak_conf.h"
#include "boot_util.h"
#include "boot_sha256.h"

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        s++;
        digits++;
        if (digits > 10)
            return -1;
    }
    if (!digits)
        return -1;
    *out = v;
    return 0;
}

static int parse_ipv4(const char *s, uint32_t *out) {
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
    if (*s && !is_space(*s))
        return -1;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

static int key_eq(const char *k, size_t kl, const char *name) {
    size_t n = boot_strlen(name);
    if (kl != n)
        return 0;
    return boot_strncasecmp(k, name, n) == 0;
}

void peak_conf_defaults(struct peak_loader_conf *out) {
    boot_memset(out, 0, sizeof(*out));
    out->width = 1920;
    out->height = 1080;
    out->bpp = 32;
    out->net.mode = PEAK_NET_DHCP_FALLBACK;
    out->net.ip = 0x0A00020Fu;   /* 10.0.2.15 */
    out->net.mask = 0xFFFFFF00u; /* 255.255.255.0 */
    out->net.gw = 0x0A000202u;   /* 10.0.2.2 */
    out->net.dns = 0x0A000203u;  /* 10.0.2.3 */
    out->net.dhcp_timeout_ticks = 300; /* ~3s at 100Hz */
}

void peak_conf_parse(const char *text, size_t len, struct peak_loader_conf *out) {
    if (!out)
        return;
    peak_conf_defaults(out);
    if (!text || !len)
        return;

    size_t i = 0;
    while (i < len) {
        while (i < len && (text[i] == '\n' || text[i] == '\r'))
            i++;
        if (i >= len)
            break;
        size_t line = i;
        while (i < len && text[i] != '\n' && text[i] != '\r')
            i++;
        size_t end = i;
        /* trim */
        while (line < end && is_space(text[line]))
            line++;
        while (end > line && is_space(text[end - 1]))
            end--;
        if (line >= end || text[line] == '#')
            continue;

        size_t eq = line;
        while (eq < end && text[eq] != '=')
            eq++;
        if (eq >= end)
            continue;
        size_t key_end = eq;
        while (key_end > line && is_space(text[key_end - 1]))
            key_end--;
        size_t val = eq + 1;
        while (val < end && is_space(text[val]))
            val++;

        char key[48];
        size_t kl = key_end - line;
        if (kl >= sizeof(key))
            kl = sizeof(key) - 1;
        boot_memcpy(key, text + line, kl);
        key[kl] = '\0';

        char value[64];
        size_t vl = end - val;
        if (vl >= sizeof(value))
            vl = sizeof(value) - 1;
        boot_memcpy(value, text + val, vl);
        value[vl] = '\0';

        if (key_eq(key, kl, "resolution")) {
            /* WxH */
            uint32_t w = 0, h = 0;
            const char *p = value;
            if (parse_u32(p, &w) == 0) {
                while (*p && *p != 'x' && *p != 'X')
                    p++;
                if (*p == 'x' || *p == 'X') {
                    p++;
                    if (parse_u32(p, &h) == 0 && w > 0 && h > 0 &&
                        w <= 7680 && h <= 4320) {
                        out->width = (uint16_t)w;
                        out->height = (uint16_t)h;
                    }
                }
            }
        } else if (key_eq(key, kl, "bpp")) {
            uint32_t b = 0;
            /* Peak framebuffer path is 32bpp-only; ignore other values. */
            if (parse_u32(value, &b) == 0 && b == 32)
                out->bpp = 32;
        } else if (key_eq(key, kl, "net_mode")) {
            if (boot_strncasecmp(value, "static", 6) == 0)
                out->net.mode = PEAK_NET_STATIC;
            else if (boot_strncasecmp(value, "dhcp_only", 9) == 0)
                out->net.mode = PEAK_NET_DHCP_ONLY;
            else
                out->net.mode = PEAK_NET_DHCP_FALLBACK;
        } else if (key_eq(key, kl, "net_ip")) {
            uint32_t ip = 0;
            if (parse_ipv4(value, &ip) == 0)
                out->net.ip = ip;
        } else if (key_eq(key, kl, "net_mask")) {
            uint32_t m = 0;
            if (parse_ipv4(value, &m) == 0)
                out->net.mask = m;
        } else if (key_eq(key, kl, "net_gw")) {
            uint32_t g = 0;
            if (parse_ipv4(value, &g) == 0)
                out->net.gw = g;
        } else if (key_eq(key, kl, "net_dns")) {
            uint32_t d = 0;
            if (parse_ipv4(value, &d) == 0)
                out->net.dns = d;
        } else if (key_eq(key, kl, "dhcp_timeout_ticks")) {
            uint32_t t = 0;
            if (parse_u32(value, &t) == 0 && t > 0 && t < 100000)
                out->net.dhcp_timeout_ticks = t;
        } else if (key_eq(key, kl, "smoke_persist")) {
            uint32_t v = 0;
            if (parse_u32(value, &v) == 0 && v != 0)
                out->smoke_persist = 1;
        } else if (key_eq(key, kl, "verify_required")) {
            uint32_t v = 0;
            if (parse_u32(value, &v) == 0 && v != 0)
                out->verify_required = 1;
        } else if (key_eq(key, kl, "kernel_sha256")) {
            size_t n = boot_strlen(value);
            if (n == BOOT_SHA256_DIGEST_LEN * 2) {
                boot_memcpy(out->kernel_sha256, value, n);
                out->kernel_sha256[n] = '\0';
            }
        }
    }
}
