#ifndef PEAK_HTTP2_H
#define PEAK_HTTP2_H

#include "types.h"

/* Stored response body cap (coordinated with PEAK 64 KiB IO policy). */
#define HTTP2_BODY_MAX 65536
/* RFC 7540 default MAX_FRAME_SIZE. */
#define HTTP2_MAX_FRAME 16384
#define HTTP2_TRACE_MAX 12

struct http2_frame_trace {
    uint8_t type;
    uint8_t flags;
    uint32_t sid;
    uint32_t plen;
};

struct http2_meta {
    int status;
    size_t body_stored;
    size_t body_total;
    int truncated;
    size_t message_len; /* full HTTP/1.0 message bytes written to out */
    uint8_t saw_status;
    uint8_t headers_end_stream;
    uint8_t rst;
    uint8_t goaway;
    uint16_t data_frames;
    uint16_t frames_in;
    uint8_t first_hpack[4];
    uint8_t ntrace;
    struct http2_frame_trace trace[HTTP2_TRACE_MAX];
};

int http2_get(const char *host, const char *path, const char *extra_headers,
              char *out, size_t out_cap, int *status_out);

int http2_request(const char *method, const char *host, const char *path,
                  const char *extra_headers, const char *body, size_t body_len,
                  char *out, size_t out_cap, int *status_out);

void http2_last_meta(struct http2_meta *out);

#endif
