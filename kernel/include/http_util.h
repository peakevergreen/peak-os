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

#endif
