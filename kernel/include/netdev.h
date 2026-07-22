#ifndef PEAK_NETDEV_H
#define PEAK_NETDEV_H

#include "types.h"

/* Lifecycle: absent → present (hw seen) → probed (init ran) → ready (I/O ok). */
enum netdev_state {
    NETDEV_ABSENT = 0,
    NETDEV_PRESENT = 1,
    NETDEV_PROBED = 2,
    NETDEV_READY = 3,
};

struct netdev_ops {
    const char *name;
    int  (*init)(void);
    int  (*ready)(void); /* non-zero only when NETDEV_READY */
    void (*get_mac)(uint8_t mac[6]);
    int  (*send)(const void *data, uint16_t len);
    int  (*recv)(void *buf, uint16_t cap);
    int  (*rx_pending)(void);
};

void netdev_register(const struct netdev_ops *ops);
const struct netdev_ops *netdev_get(void);
int  netdev_init(void);
int  netdev_ready(void);
void netdev_get_mac(uint8_t mac[6]);
int  netdev_send(const void *data, uint16_t len);
int  netdev_recv(void *buf, uint16_t cap);
int  netdev_rx_pending(void);

/* Built-in backends */
void netdev_register_e1000(void);
void netdev_register_usb_lan(void);
void netdev_register_genet(void);
void netdev_register_rp1_eth(void);

#endif
