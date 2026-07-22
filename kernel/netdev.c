#include "netdev.h"
#include "serial.h"

static const struct netdev_ops *g_nd;

void netdev_register(const struct netdev_ops *ops) {
    g_nd = ops;
    if (ops) {
        serial_write_str("netdev: registered ");
        if (ops->name)
            serial_write_str(ops->name);
        serial_write_str("\n");
    }
}

const struct netdev_ops *netdev_get(void) {
    return g_nd;
}

int netdev_init(void) {
    if (!g_nd || !g_nd->init)
        return -1;
    return g_nd->init();
}

int netdev_ready(void) {
    return g_nd && g_nd->ready && g_nd->ready();
}

void netdev_get_mac(uint8_t mac[6]) {
    if (g_nd && g_nd->get_mac)
        g_nd->get_mac(mac);
    else {
        mac[0] = mac[1] = mac[2] = mac[3] = mac[4] = mac[5] = 0;
    }
}

int netdev_send(const void *data, uint16_t len) {
    if (!g_nd || !g_nd->send)
        return -1;
    return g_nd->send(data, len);
}

int netdev_recv(void *buf, uint16_t cap) {
    if (!g_nd || !g_nd->recv)
        return -1;
    return g_nd->recv(buf, cap);
}

int netdev_rx_pending(void) {
    if (!g_nd || !g_nd->rx_pending)
        return 0;
    return g_nd->rx_pending();
}
