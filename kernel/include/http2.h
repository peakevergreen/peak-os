#ifndef PEAK_HTTP2_H
#define PEAK_HTTP2_H

#include "types.h"

/* Stored response body cap (coordinated with PEAK 64 KiB IO policy). */
#define HTTP2_BODY_MAX 65536
/* RFC 7540 default MAX_FRAME_SIZE. */
#define HTTP2_MAX_FRAME 16384

struct http2_meta {
    int status;
    size_t body_stored;
    size_t body_total;
    int truncated;
    size_t message_len; /* full HTTP/1.0 message bytes written to out */
};

int http2_get(const char *host, const char *path, const char *extra_headers,
              char *out, size_t out_cap, int *status_out);

int http2_request(const char *method, const char *host, const char *path,
                  const char *extra_headers, const char *body, size_t body_len,
                  char *out, size_t out_cap, int *status_out);

void http2_last_meta(struct http2_meta *out);

#endif
