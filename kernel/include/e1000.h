#ifndef PEAK_E1000_H
#define PEAK_E1000_H

#include "types.h"

int  e1000_init(void);
int  e1000_send(const void *data, uint16_t len);
/* Non-blocking: copy one RX frame into buf, return length or -1 if empty */
int  e1000_recv(void *buf, uint16_t cap);
int  e1000_rx_pending(void); /* non-zero if a frame is waiting */
void e1000_get_mac(uint8_t mac[6]);
int  e1000_ready(void);

struct e1000_stats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
};
void e1000_get_stats(struct e1000_stats *out);

#endif
