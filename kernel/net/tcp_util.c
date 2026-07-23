#ifdef PEAK_HOST_TEST
#include "../include/tcp_util.h"
#include <string.h>
#else
#include "tcp_util.h"
#include "util.h"
#endif

uint16_t net_checksum(const void *data, size_t len) {
    const uint8_t *p = data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

uint16_t net_tcp_checksum(uint32_t src, uint32_t dst, const void *tcp, size_t tcp_len) {
    uint32_t sum = 0;
    sum += (src >> 16) & 0xFFFF;
    sum += src & 0xFFFF;
    sum += (dst >> 16) & 0xFFFF;
    sum += dst & 0xFFFF;
    sum += 6; /* IPPROTO_TCP */
    sum += (uint32_t)tcp_len;
    const uint8_t *p = tcp;
    size_t len = tcp_len;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

int tcp_parse_header(const uint8_t *pkt, uint16_t len, struct tcp_hdr_info *out) {
    if (!pkt || !out || len < 20)
        return -1;
    uint8_t data_off = (uint8_t)((pkt[12] >> 4) * 4);
    /* Data offset is in 32-bit words; minimum legal header is 5 words (20 bytes). */
    if (data_off < 20 || data_off > len)
        return -1;
    out->sport = (uint16_t)(((uint16_t)pkt[0] << 8) | pkt[1]);
    out->dport = (uint16_t)(((uint16_t)pkt[2] << 8) | pkt[3]);
    out->seq = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) |
               ((uint32_t)pkt[6] << 8) | pkt[7];
    out->ack = ((uint32_t)pkt[8] << 24) | ((uint32_t)pkt[9] << 16) |
               ((uint32_t)pkt[10] << 8) | pkt[11];
    out->data_off = data_off;
    out->flags = pkt[13];
    out->window = (uint16_t)(((uint16_t)pkt[14] << 8) | pkt[15]);
    out->dlen = (uint16_t)(len - data_off);
    out->data = pkt + data_off;
    return 0;
}
