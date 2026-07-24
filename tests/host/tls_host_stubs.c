/*
 * Host stubs for linking kernel/net/tls_trust.c under PEAK_HOST_TEST.
 * In-memory TOFU store replaces VFS; session pin/hostname state is shared
 * with the trust module via tls_internal.h symbols.
 */
#include "tls_internal.h"
#include "vfs.h"
#include "rtc.h"
#include "util.h"

#include <string.h>

/* Session state normally defined in tls.c — only what tls_trust needs. */
int cert_verified;
int hostname_matched;
int hostname_parse_skipped;
const char *cert_fail_reason;
uint8_t trust_pins[TLS_PIN_MAX][32];
int trust_pin_count;
char last_err[96];
int last_err_code;

/* Needed by tls_clienthello.c when linked into host tests. */
uint8_t client_random[32];
uint8_t tls13_priv[32];
uint8_t tls13_client_pub[32];

void tls_set_err_code(int code, const char *msg) {
    size_t i = 0;
    last_err_code = code;
    if (!msg)
        msg = "unknown";
    for (; msg[i] && i + 1 < sizeof(last_err); i++)
        last_err[i] = msg[i];
    last_err[i] = '\0';
}

void tls_set_err(const char *msg) {
    tls_set_err_code(TLS_E_GENERIC, msg);
}

void tls_set_alert_err(const uint8_t *alert, size_t n) {
    uint8_t level = (n >= 1) ? alert[0] : 0;
    uint8_t desc = (n >= 2) ? alert[1] : 0;
    const char *name = "unknown";
    switch (desc) {
    case 0:  name = "close_notify"; break;
    case 10: name = "unexpected_message"; break;
    case 20: name = "bad_record_mac"; break;
    case 22: name = "record_overflow"; break;
    case 40: name = "handshake_failure"; break;
    case 42: name = "bad_certificate"; break;
    case 43: name = "unsupported_certificate"; break;
    case 44: name = "certificate_revoked"; break;
    case 45: name = "certificate_expired"; break;
    case 46: name = "certificate_unknown"; break;
    case 47: name = "illegal_parameter"; break;
    case 48: name = "unknown_ca"; break;
    case 49: name = "access_denied"; break;
    case 50: name = "decode_error"; break;
    case 51: name = "decrypt_error"; break;
    case 70: name = "protocol_version"; break;
    case 71: name = "insufficient_security"; break;
    case 80: name = "internal_error"; break;
    case 90: name = "user_canceled"; break;
    case 112: name = "unrecognized_name"; break;
    default: break;
    }
    char buf[96];
    if (desc == 0 && level == 1)
        snprintf(buf, sizeof(buf), "Server alert %s (graceful close)", name);
    else
        snprintf(buf, sizeof(buf), "Server alert %s (level %u desc %u)", name, level, desc);
    tls_set_err_code(TLS_E_ALERT, buf);
}

const char *tls_last_error(void) {
    return last_err[0] ? last_err : "no error";
}

int tls_last_error_code(void) {
    return last_err_code;
}

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
    last_err[0] = '\0';
    last_err_code = TLS_E_OK;
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

static int host_tls_tofu = 1; /* host tests default TOFU on for legacy fixtures */

int settings_tls_tofu(void) { return host_tls_tofu; }
void settings_set_tls_tofu(int on) { host_tls_tofu = on ? 1 : 0; }
void settings_toggle_tls_tofu(void) { host_tls_tofu = !host_tls_tofu; }
