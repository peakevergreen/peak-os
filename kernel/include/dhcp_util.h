#ifndef PEAK_DHCP_UTIL_H
#define PEAK_DHCP_UTIL_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#include <stddef.h>
#else
#include "types.h"
#endif

#define DHCP_MAGIC_COOKIE 0x63825363u

struct dhcp_lease {
    uint32_t yiaddr; /* offered/acked IP (host order) */
    uint32_t mask;
    uint32_t gw;
    uint32_t dns;
    uint32_t server_id;
    uint8_t msg_type; /* 2=OFFER, 5=ACK, 6=NAK */
};

/* Parse DHCP options starting after magic cookie. Returns 0 if msg_type found. */
int dhcp_parse_options(const uint8_t *opts, size_t opt_len, struct dhcp_lease *out);

/* Parse dotted IPv4 into host-order uint32. */
int peak_parse_ipv4(const char *s, uint32_t *out);

#endif
