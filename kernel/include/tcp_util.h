#ifndef PEAK_TCP_UTIL_H
#define PEAK_TCP_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#else
#include "types.h"
#endif

/* Internet checksum (ones' complement) over an arbitrary buffer. */
uint16_t net_checksum(const void *data, size_t len);

/* TCP pseudo-header + segment checksum. proto assumed IPPROTO_TCP (6). */
uint16_t net_tcp_checksum(uint32_t src, uint32_t dst, const void *tcp, size_t tcp_len);

struct tcp_hdr_info {
    uint16_t sport;
    uint16_t dport;
    uint32_t seq;
    uint32_t ack;
    uint8_t data_off; /* bytes */
    uint8_t flags;
    uint16_t window;
    uint16_t dlen;
    const uint8_t *data;
};

/*
 * Parse a TCP segment header. Rejects truncated headers and illegal data
 * offsets (< 20 or > len). Returns 0 on success, -1 if malformed.
 */
int tcp_parse_header(const uint8_t *pkt, uint16_t len, struct tcp_hdr_info *out);

#endif
