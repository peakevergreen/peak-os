#ifndef PEAK_NET_H
#define PEAK_NET_H

#include "types.h"
#include "peak_boot.h"

/* Peak in-guest network (e1000 + IPv4/TCP/DNS/HTTP + TCP listen). */

struct net_info {
    int up;
    uint8_t mac[6];
    uint32_t ip;      /* host order */
    uint32_t mask;
    uint32_t gw;
    uint32_t dns;
    const char *driver;
    const char *addr_mode; /* "dhcp" | "static" | "fallback" */
};

void net_set_boot_config(const struct peak_net_config *cfg);

int  net_init(void);
void net_poll(void);
int  net_ready(void);
void net_get_info(struct net_info *out);

/* DNS A record. Returns host-order IPv4 or 0 on failure. */
uint32_t net_dns_resolve(const char *hostname, uint32_t timeout_ticks);

#define NET_TCP_MAX     8
#define NET_LISTEN_MAX  4

/*
 * TCP helpers (up to NET_TCP_MAX concurrent connections).
 * Legacy APIs use the most recently connected client slot.
 */
int net_tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ticks);
int net_tcp_send(const void *data, size_t len);
int net_tcp_recv(void *buf, size_t cap, size_t *out_len, uint32_t timeout_ticks);
void net_tcp_close(void);
int net_tcp_active_count(void);

/* Passive server API (non-blocking accept). listen returns listener id >= 0. */
int net_tcp_listen(uint16_t port);
void net_tcp_unlisten(uint16_t port);
int net_tcp_listening(uint16_t port);
int net_tcp_accept(int listen_id); /* returns conn fd or -1 */
int net_tcp_fd_send(int fd, const void *data, size_t len);
int net_tcp_fd_recv(int fd, void *buf, size_t cap, size_t *out_len,
                    uint32_t timeout_ticks);
void net_tcp_fd_close(int fd);

/* DHCP with configured fallback (see peak_net_config). */
int net_dhcp_try(uint32_t timeout_ticks);

struct net_http_request {
    char method[8];
    const char *url;
    const char *headers; /* extra header lines, CRLF-separated, no blank line */
    const char *body;
    size_t body_len;
};

struct net_http_response {
    int status;
    char *headers;
    char *body;
    size_t body_len;
};

/*
 * HTTP/HTTPS client (GET/POST). Follows redirects (max 5).
 * Response headers copied to hdr_out (NUL-terminated). Body in body.
 * HTTPS uses in-guest TLS 1.2. Only 2xx counts as success.
 */
int net_http_request(const struct net_http_request *req, char *body, size_t body_cap,
                     int *status_out, char *hdr_out, size_t hdr_cap);

/* Thin wrapper around net_http_request (GET). */
int net_http_get(const char *url, char *body, size_t body_cap, int *status_out);

/* Non-zero if last failure was because the target needs HTTPS/TLS. */
int net_http_needs_tls(void);

/* Format IPv4 host-order into buf "a.b.c.d" */
void net_format_ip(uint32_t ip, char *buf, size_t cap);

/* Counters for regression: GUI idle must not bump these. */
struct net_attempt_stats {
    uint64_t dns;
    uint64_t tcp;
    uint64_t http;
    uint64_t tls;
};
void net_attempt_stats_get(struct net_attempt_stats *out);
void net_attempt_stats_reset(void);
void net_attempt_stats_note_tls(void);

#endif
