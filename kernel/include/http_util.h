#ifndef PEAK_HTTP_UTIL_H
#define PEAK_HTTP_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#else
#include "types.h"
#endif

/* Normalize URL path: collapse //, resolve ., reject .. escape. Returns 0 ok. */
int http_normalize_path(const char *in, char *out, size_t out_cap);

/* MIME type for a path (extension-based). */
const char *http_mime_for_path(const char *path);

/*
 * Parse first request line: METHOD SP PATH SP HTTP/x.y
 * Returns 0 on success.
 */
int http_parse_request_line(const char *req, char *method, size_t method_cap,
                            char *path, size_t path_cap);

/* scheme://host:port from absolute URL. Returns 0 on success. */
int http_parse_origin(const char *url, char *origin, size_t cap);

int http_same_origin(const char *a, const char *b);

/* Simple CORS: ACAO=* (non-credentialed only) or exact origin match. */
int http_cors_allows(const char *page_origin, const char *resp_acao, int credentialed);

/* Resolve rel against base URL (RFC3986-ish). Returns 0 on success. */
int http_resolve_url(const char *base, const char *rel, char *out, size_t out_cap);

/*
 * Parse http(s) URL into host/port/path. Rejects missing host and ports > 65535.
 * Host stops at /, ?, #, or :. Returns 0 on success.
 */
int http_parse_url(const char *url, int *https, char *host, size_t host_cap,
                   uint16_t *port, char *path, size_t path_cap);

/*
 * Length of the header block including the final \r\n\r\n, or 0 if the
 * delimiter is not yet present (partial read).
 */
size_t http_headers_len(const char *buf, size_t len);

/* Case-insensitive header lookup in a raw response. Returns 0 if found. */
int http_header_value(const char *raw, const char *key, char *out, size_t out_cap);

/* Copy header block (through \r\n\r\n) into hdr_out. No-op if incomplete. */
void http_copy_response_headers(const char *buf, char *hdr_out, size_t hdr_cap);

/* Strip headers in-place, leaving the body. No-op if incomplete. */
void http_strip_headers(char *msg);

/* Parse "HTTP/x.y NNN ..." status. Returns 0 and sets *status_out on success. */
int http_parse_status(const char *buf, int *status_out);

int http_is_redirect(int status);

/*
 * Resolve a Location against the current request URL parts.
 * Absolute http(s) locations are copied; otherwise resolved via http_resolve_url
 * so non-default ports are preserved.
 */
int http_join_redirect(int https, const char *host, uint16_t port,
                       const char *cur_path, const char *loc, char *out,
                       size_t out_cap);

#endif
