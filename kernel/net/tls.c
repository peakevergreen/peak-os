#include "tls_internal.h"
#include "net.h"
#include "random.h"
#include "serial.h"
#include "timer.h"
#include "util.h"

/* Session state — shared across tls_*.c via tls_internal.h. */
int tls_up;
int tls13;
int cipher_kind;
int use_ems;
int cert_verified;
int hostname_matched;
int hostname_parse_skipped;
const char *cert_fail_reason;
char last_err[96];
int last_err_code;
uint8_t trust_pins[TLS_PIN_MAX][32];
int trust_pin_count;
uint8_t client_random[32];
uint8_t server_random[32];
uint8_t master_secret[48];
uint8_t client_key[32], server_key[32];
uint8_t client_iv[12], server_iv[12];
uint64_t client_seq, server_seq;
struct sha256_ctx transcript;
struct sha384_ctx transcript384;
uint8_t rx_app[16384];
size_t rx_app_len;
uint8_t hs_reasm[24576];
size_t hs_reasm_len;
uint8_t tls13_priv[32];
uint8_t tls13_client_pub[32];
uint8_t tls13_server_pub[32];
uint8_t tls13_early_secret[48];
uint8_t tls13_handshake_secret[48];
uint8_t tls13_master_secret[48];
uint8_t tls13_client_hs_traffic[48];
uint8_t tls13_server_hs_traffic[48];
uint8_t tls13_client_app_traffic[48];
uint8_t tls13_server_app_traffic[48];
size_t tls13_hash_len;
int tls13_sha384;
char tls_alpn[16];
int tls_alpn_h2;

void tls_alpn_clear(void) {
    tls_alpn[0] = '\0';
    tls_alpn_h2 = 0;
}

int tls_alpn_is_h2(void) {
    return tls_alpn_h2;
}

void tls_alpn_set_from_ext(const uint8_t *data, size_t len) {
    /* ALPN extension data: list_len(2) + name_len(1) + name */
    if (!data || len < 3)
        return;
    uint16_t list = ((uint16_t)data[0] << 8) | data[1];
    if (list + 2 > len || list < 1)
        return;
    uint8_t nlen = data[2];
    if (3 + nlen > len || nlen == 0 || nlen >= sizeof(tls_alpn))
        return;
    memcpy(tls_alpn, data + 3, nlen);
    tls_alpn[nlen] = '\0';
    tls_alpn_h2 = (nlen == 2 && tls_alpn[0] == 'h' && tls_alpn[1] == '2');
}

void tls_set_err(const char *msg) {
    tls_set_err_code(TLS_E_GENERIC, msg);
}

void tls_set_err_code(int code, const char *msg) {
    size_t i = 0;
    last_err_code = code;
    if (!msg)
        msg = "unknown";
    for (; msg[i] && i + 1 < sizeof(last_err); i++)
        last_err[i] = msg[i];
    last_err[i] = '\0';
    char line[112];
    snprintf(line, sizeof(line), "tls: %s\n", last_err);
    serial_log(SERIAL_LOG_WARN, line);
}

const char *tls_last_error(void) {
    return last_err[0] ? last_err : "no error";
}

int tls_last_error_code(void) {
    return last_err_code;
}

int tls_ready(void) {
    return tls_up;
}

int tls_send(const void *data, size_t len) {
    if (!tls_up)
        return -1;
    const uint8_t *p = data;
    while (len) {
        size_t n = len > 1400 ? 1400 : len;
        if (tls_send_record(TLS_CONTENT_APP, p, n, 1) != 0)
            return -1;
        p += n;
        len -= n;
    }
    return 0;
}

int tls_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks) {
    if (out_len)
        *out_len = 0;
    if (!tls_up)
        return -1;

    if (rx_app_len > 0) {
        size_t n = rx_app_len < cap ? rx_app_len : cap;
        memcpy(buf, rx_app, n);
        memmove(rx_app, rx_app + n, rx_app_len - n);
        rx_app_len -= n;
        if (out_len)
            *out_len = n;
        return 0;
    }

    uint64_t start = timer_ticks();
    while (timer_ticks() - start < timeout_ticks) {
        uint8_t type;
        size_t n = 0;
        /* Decrypt straight into rx_app — it is sized for a max TLS record
         * (16384), so nothing is silently dropped. */
        if (tls_recv_record(&type, rx_app, sizeof(rx_app), &n, 50, 1) != 0) {
            net_poll();
            continue;
        }
        if (type == TLS_CONTENT_ALERT) {
            /* close_notify (or fatal alert): session is over. Mark it so
             * callers can tell "stream ended" from "momentary stall". */
            tls_up = 0;
            return -1;
        }
        if (type != TLS_CONTENT_APP)
            continue;
        rx_app_len = n;
        size_t take = rx_app_len < cap ? rx_app_len : cap;
        memcpy(buf, rx_app, take);
        memmove(rx_app, rx_app + take, rx_app_len - take);
        rx_app_len -= take;
        if (out_len)
            *out_len = take;
        return 0;
    }
    return -1;
}

void tls_scrub_secrets(void) {
    memzero_explicit(client_random, sizeof(client_random));
    memzero_explicit(server_random, sizeof(server_random));
    memzero_explicit(master_secret, sizeof(master_secret));
    memzero_explicit(client_key, sizeof(client_key));
    memzero_explicit(server_key, sizeof(server_key));
    memzero_explicit(client_iv, sizeof(client_iv));
    memzero_explicit(server_iv, sizeof(server_iv));
    memzero_explicit(tls13_priv, sizeof(tls13_priv));
    memzero_explicit(tls13_client_pub, sizeof(tls13_client_pub));
    memzero_explicit(tls13_server_pub, sizeof(tls13_server_pub));
    memzero_explicit(tls13_early_secret, sizeof(tls13_early_secret));
    memzero_explicit(tls13_handshake_secret, sizeof(tls13_handshake_secret));
    memzero_explicit(tls13_master_secret, sizeof(tls13_master_secret));
    memzero_explicit(tls13_client_hs_traffic, sizeof(tls13_client_hs_traffic));
    memzero_explicit(tls13_server_hs_traffic, sizeof(tls13_server_hs_traffic));
    memzero_explicit(tls13_client_app_traffic, sizeof(tls13_client_app_traffic));
    memzero_explicit(tls13_server_app_traffic, sizeof(tls13_server_app_traffic));
}

void tls_close(void) {
    if (tls_up) {
        uint8_t alert[2] = {1, 0};
        tls_send_record(TLS_CONTENT_ALERT, alert, 2, 1);
    }
    net_tcp_close();
    tls_up = 0;
    tls13 = 0;
    cert_verified = 0;
    rx_app_len = 0;
    hs_reasm_len = 0;
    client_seq = server_seq = 0;
    tls_scrub_secrets();
}
