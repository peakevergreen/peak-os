/*
 * Host stubs for linking kernel/net/tls_trust.c under PEAK_HOST_TEST.
 * In-memory TOFU store replaces VFS; session pin/hostname state is shared
 * with the trust module via tls_internal.h symbols.
 */
#include "tls_internal.h"
#include "vfs.h"
#include "rtc.h"

#include <string.h>

/* Session state normally defined in tls.c — only what tls_trust needs. */
int cert_verified;
int hostname_matched;
int hostname_parse_skipped;
const char *cert_fail_reason;
uint8_t trust_pins[TLS_PIN_MAX][32];
int trust_pin_count;

/* Needed by tls_clienthello.c when linked into host tests. */
uint8_t client_random[32];
uint8_t tls13_priv[32];
uint8_t tls13_client_pub[32];

#define HOST_TOFU_CAP 4096
static char host_tofu[HOST_TOFU_CAP];
static size_t host_tofu_len;

void tls_host_reset_trust(void) {
    trust_pin_count = 0;
    memset(trust_pins, 0, sizeof(trust_pins));
    cert_verified = 0;
    hostname_matched = 0;
    hostname_parse_skipped = 0;
    cert_fail_reason = NULL;
    host_tofu_len = 0;
    host_tofu[0] = '\0';
}

void tls_host_seed_tofu(const char *store) {
    if (!store) {
        host_tofu_len = 0;
        host_tofu[0] = '\0';
        return;
    }
    size_t n = strlen(store);
    if (n >= HOST_TOFU_CAP)
        n = HOST_TOFU_CAP - 1;
    memcpy(host_tofu, store, n);
    host_tofu[n] = '\0';
    host_tofu_len = n;
}

const char *tls_host_tofu_store(void) {
    return host_tofu;
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    if (!path || strcmp(path, TOFU_PATH) != 0)
        return -1;
    if (!data && len)
        return -1;
    if (len >= HOST_TOFU_CAP)
        return -1;
    if (len)
        memcpy(host_tofu, data, len);
    host_tofu[len] = '\0';
    host_tofu_len = len;
    return 0;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    if (!path || strcmp(path, TOFU_PATH) != 0)
        return -1;
    if (!buf || !out_len)
        return -1;
    size_t n = host_tofu_len;
    if (n > buf_len)
        n = buf_len;
    if (n)
        memcpy(buf, host_tofu, n);
    *out_len = n;
    return 0;
}

/* Host RTC: fixed "now" inside the example leaf validity window. */
int rtc_read(struct rtc_time *out) {
    if (!out)
        return -1;
    out->sec = 0;
    out->min = 0;
    out->hour = 12;
    out->day = 1;
    out->month = 8;
    out->year = 2026;
    return 0;
}
