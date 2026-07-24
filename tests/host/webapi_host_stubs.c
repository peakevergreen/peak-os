/*
 * Host stubs for linking kernel/gui/webapi*.c under PEAK_HOST_TEST.
 * Canned HTTP responses let fetch stub paths run without a real network.
 */
#include "net.h"

#include <stdio.h>
#include <string.h>

static int g_http_rc = -1;
static int g_http_status = 0;
static int g_needs_tls;
static char g_http_body[512];
static char g_http_headers[256];
static char g_tls_reject[64] = "fetch: tls-handshake";

void webapi_host_set_http(int rc, int status, const char *body, const char *headers) {
    g_http_rc = rc;
    g_http_status = status;
    g_http_body[0] = '\0';
    g_http_headers[0] = '\0';
    if (body)
        snprintf(g_http_body, sizeof(g_http_body), "%s", body);
    if (headers)
        snprintf(g_http_headers, sizeof(g_http_headers), "%s", headers);
}

void webapi_host_set_tls_fail(const char *reject_name) {
    g_needs_tls = 1;
    g_http_rc = -1;
    g_http_status = 0;
    g_http_body[0] = '\0';
    if (reject_name)
        snprintf(g_tls_reject, sizeof(g_tls_reject), "%s", reject_name);
}

void webapi_host_clear_tls(void) {
    g_needs_tls = 0;
}

int net_http_request(const struct net_http_request *req, char *body, size_t body_cap,
                     int *status_out, char *hdr_out, size_t hdr_cap) {
    (void)req;
    if (status_out)
        *status_out = g_http_status;
    if (body && body_cap)
        snprintf(body, body_cap, "%s", g_http_body);
    if (hdr_out && hdr_cap)
        snprintf(hdr_out, hdr_cap, "%s", g_http_headers);
    return g_http_rc;
}

int net_http_get(const char *url, char *body, size_t body_cap, int *status_out) {
    struct net_http_request req;
    memset(&req, 0, sizeof(req));
    snprintf(req.method, sizeof(req.method), "GET");
    req.url = url;
    return net_http_request(&req, body, body_cap, status_out, NULL, 0);
}

int net_http_needs_tls(void) {
    return g_needs_tls;
}

int net_http_last_tls_secure(void) {
    return 0;
}

int net_http_last_tls_verified(void) {
    return 0;
}

const char *net_http_tls_reject_name(void) {
    return g_tls_reject;
}
