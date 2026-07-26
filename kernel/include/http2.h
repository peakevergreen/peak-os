#ifndef PEAK_HTTP2_H
#define PEAK_HTTP2_H

#include "types.h"

#define HTTP2_BODY_MAX 12288

struct http2_meta {
    int status;
    size_t body_stored;
    size_t body_total;
    int truncated;
};

int http2_get(const char *host, const char *path, const char *extra_headers,
              char *out, size_t out_cap, int *status_out);

int http2_request(const char *method, const char *host, const char *path,
                  const char *extra_headers, const char *body, size_t body_len,
                  char *out, size_t out_cap, int *status_out);

void http2_last_meta(struct http2_meta *out);

#endif
