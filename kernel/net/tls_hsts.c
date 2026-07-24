/*
 * Bounded HSTS-lite map on PeakFS (/etc/peak/tls-hsts).
 * Lines: host:expiry_ticks\n  (timer_ticks() units, 100 Hz).
 */
#include "tls_hsts.h"
#include "timer.h"
#include "util.h"
#include "vfs.h"

static char store[HSTS_MAX];
static int loaded;

static void load(void) {
    if (loaded)
        return;
    store[0] = '\0';
    size_t n = 0;
    if (vfs_read_file(HSTS_PATH, store, sizeof(store) - 1, &n) == 0)
        store[n] = '\0';
    loaded = 1;
}

static void persist(void) {
    size_t n = strlen(store);
    vfs_write_file(HSTS_PATH, store, n);
}

static void copy_host_lc(char *dst, size_t cap, const char *host) {
    size_t i = 0;
    if (!host)
        host = "";
    for (; host[i] && i + 1 < cap; i++) {
        char c = host[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        dst[i] = c;
    }
    dst[i] = '\0';
}

void hsts_clear(void) {
    store[0] = '\0';
    loaded = 1;
    vfs_write_file(HSTS_PATH, "", 0);
}

void hsts_process_header(const char *host, const char *raw_headers) {
    if (!host || !host[0] || !raw_headers)
        return;
    const char *p = raw_headers;
    int max_age = -1;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n')
            p++;
        size_t llen = (size_t)(p - line);
        if (llen >= 2 && line[llen - 1] == '\r')
            llen--;
        if (llen > 24) {
            /* Strict-Transport-Security: max-age=N */
            char key[28];
            size_t k = 0;
            while (k < llen && k + 1 < sizeof(key) && line[k] != ':') {
                char c = line[k];
                if (c >= 'A' && c <= 'Z')
                    c = (char)(c - 'A' + 'a');
                key[k++] = c;
            }
            key[k] = '\0';
            if (!strcmp(key, "strict-transport-security") && k < llen && line[k] == ':') {
                const char *v = line + k + 1;
                while (*v == ' ')
                    v++;
                if (!strncmp(v, "max-age=", 8)) {
                    v += 8;
                    max_age = 0;
                    while (*v >= '0' && *v <= '9') {
                        max_age = max_age * 10 + (*v - '0');
                        v++;
                    }
                }
            }
        }
        if (*p == '\n')
            p++;
    }
    if (max_age < 0)
        return;
    load();
    char h[128];
    copy_host_lc(h, sizeof(h), host);
    if (max_age == 0) {
        /* Delete entry */
        char out[HSTS_MAX];
        size_t o = 0;
        const char *s = store;
        while (*s) {
            const char *eol = s;
            while (*eol && *eol != '\n')
                eol++;
            size_t hl = strlen(h);
            if (!(eol > s + (size_t)hl && s[hl] == ':' && !memcmp(s, h, hl))) {
                size_t ln = (size_t)(eol - s);
                if (o + ln + 2 < sizeof(out)) {
                    memcpy(out + o, s, ln);
                    o += ln;
                    out[o++] = '\n';
                }
            }
            s = *eol ? eol + 1 : eol;
        }
        out[o] = '\0';
        memcpy(store, out, o + 1);
        persist();
        return;
    }
    uint32_t expiry = (uint32_t)timer_ticks() + (uint32_t)max_age * 100u;
    char line[160];
    snprintf(line, sizeof(line), "%s:%u\n", h, expiry);
    size_t add = strlen(line);
    /* Remove old entry for host */
    char tmp[HSTS_MAX];
    size_t o = 0;
    const char *s = store;
    while (*s) {
        const char *eol = s;
        while (*eol && *eol != '\n')
            eol++;
        size_t hl = strlen(h);
        if (!(eol > s + (size_t)hl && s[hl] == ':' && !memcmp(s, h, hl))) {
            size_t ln = (size_t)(eol - s);
            if (o + ln + 2 < sizeof(tmp)) {
                memcpy(tmp + o, s, ln);
                o += ln;
                tmp[o++] = '\n';
            }
        }
        s = *eol ? eol + 1 : eol;
    }
    if (o + add >= sizeof(tmp))
        return; /* refuse when full */
    memcpy(tmp + o, line, add + 1);
    memcpy(store, tmp, o + add + 1);
    persist();
}

int hsts_should_upgrade(const char *host) {
    if (!host || !host[0])
        return 0;
    load();
    char h[128];
    copy_host_lc(h, sizeof(h), host);
    uint32_t now = (uint32_t)timer_ticks();
    const char *s = store;
    while (*s) {
        const char *eol = s;
        while (*eol && *eol != '\n')
            eol++;
        size_t hl = strlen(h);
        if (eol > s + (size_t)hl && s[hl] == ':' && !memcmp(s, h, hl)) {
            uint32_t exp = 0;
            const char *n = s + hl + 1;
            while (n < eol && *n >= '0' && *n <= '9') {
                exp = exp * 10u + (uint32_t)(*n - '0');
                n++;
            }
            return exp > now;
        }
        s = *eol ? eol + 1 : eol;
    }
    return 0;
}

#ifdef PEAK_HOST_TEST
void hsts_host_put(const char *host, uint32_t expiry_ticks) {
    load();
    char h[128];
    copy_host_lc(h, sizeof(h), host);
    char line[160];
    snprintf(line, sizeof(line), "%s:%u\n", h, expiry_ticks);
    size_t add = strlen(line);
    size_t sl = strlen(store);
    if (sl + add >= sizeof(store))
        return;
    memcpy(store + sl, line, add + 1);
    persist();
}

int hsts_host_test_count(void) {
    load();
    int n = 0;
    for (const char *s = store; *s; s++)
        if (*s == '\n')
            n++;
    return n;
}
#endif
