#include "netdev.h"
#include "e1000.h"

static const struct netdev_ops e1000_ops = {
    .name = "e1000",
    .init = e1000_init,
    .ready = e1000_ready,
    .get_mac = e1000_get_mac,
    .send = e1000_send,
    .recv = e1000_recv,
    .rx_pending = e1000_rx_pending,
};

void netdev_register_e1000(void) {
    netdev_register(&e1000_ops);
}

void netdev_register_e1000_fallback(void) {
    netdev_register_fallback(&e1000_ops);
}
