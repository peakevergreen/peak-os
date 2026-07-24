#ifndef PEAK_HTTP2_H
#define PEAK_HTTP2_H

#include "types.h"

/* Minimal HTTP/2 GET over an established TLS session (ALPN h2).
 * Writes a synthetic HTTP/1.x response (status line + headers + body) into out
 * so existing http_parse_status / strip paths keep working.
 * Returns 0 on success.
 */
int http2_get(const char *host, const char *path, const char *extra_headers,
              char *out, size_t out_cap, int *status_out);

#endif
