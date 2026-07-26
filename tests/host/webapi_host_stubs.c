/*
 * Host stubs for linking kernel/gui/webapi*.c under PEAK_HOST_TEST.
 * Canned HTTP responses let fetch stub paths run without a real network.
 * Minimal VFS stubs back localStorage persistence tests.
 */
#include "net.h"
#include "vfs.h"

#include <stdio.h>
#include <string.h>

#define VFS_STUB_MAX 8
#define VFS_STUB_DATA 8192

static struct {
    char path[VFS_PATH_MAX];
    char data[VFS_STUB_DATA];
    size_t len;
} vfs_stub_files[VFS_STUB_MAX];

void webapi_host_vfs_reset(void) {
    memset(vfs_stub_files, 0, sizeof(vfs_stub_files));
}

struct vfs_node *vfs_mkdir(const char *path) {
    (void)path;
    return (struct vfs_node *)(uintptr_t)1;
}

int vfs_write_file(const char *path, const void *data, size_t len) {
    if (!path) return -1;
    if (len > VFS_STUB_DATA) len = VFS_STUB_DATA;
    for (int i = 0; i < VFS_STUB_MAX; i++) {
        if (vfs_stub_files[i].path[0] && !strcmp(vfs_stub_files[i].path, path)) {
            vfs_stub_files[i].len = 0;
            if (data && len) { memcpy(vfs_stub_files[i].data, data, len); vfs_stub_files[i].len = len; }
            return 0;
        }
    }
    for (int i = 0; i < VFS_STUB_MAX; i++) {
        if (!vfs_stub_files[i].path[0]) {
            snprintf(vfs_stub_files[i].path, sizeof(vfs_stub_files[i].path), "%s", path);
            if (data && len) { memcpy(vfs_stub_files[i].data, data, len); vfs_stub_files[i].len = len; }
            return 0;
        }
    }
    return -1;
}

int vfs_read_file(const char *path, void *buf, size_t buf_len, size_t *out_len) {
    if (!path || !buf || !out_len) return -1;
    for (int i = 0; i < VFS_STUB_MAX; i++) {
        if (vfs_stub_files[i].path[0] && !strcmp(vfs_stub_files[i].path, path)) {
            size_t n = vfs_stub_files[i].len;
            if (n > buf_len) n = buf_len;
            if (n) memcpy(buf, vfs_stub_files[i].data, n);
            *out_len = n;
            return 0;
        }
    }
    *out_len = 0;
    return -1;
}

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
