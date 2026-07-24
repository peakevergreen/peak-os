#include "netdev.h"
#include "serial.h"
#include "util.h"

/* LAN9514 (Pi 3B) / LAN7515–LAN7800 (Pi 3B+) USB Ethernet.
 * Staged stub: depends on DWC2 hub enum + SMSC/Microchip bulk IN/OUT.
 * Never claims ready. Platform must not install until NETDEV_READY is reachable. */

static uint8_t mac[6] = { 0xb8, 0x27, 0xeb, 0x00, 0x00, 0x01 };

static int lan_init(void) {
    serial_log(SERIAL_LOG_DEBUG,
               "usb-lan: stub (not ready; hub/bulk datapath deferred)\n");
    return -1;
}

static int lan_ready(void) { return 0; }
static void lan_get_mac(uint8_t m[6]) { memcpy(m, mac, 6); }
static int lan_send(const void *d, uint16_t l) {
    (void)d;
    (void)l;
    return -1;
}
static int lan_recv(void *b, uint16_t c) {
    (void)b;
    (void)c;
    return -1;
}
static int lan_rx_pending(void) { return 0; }

static const struct netdev_ops lan_ops = {
    .name = "usb-lan",
    .init = lan_init,
    .ready = lan_ready,
    .get_mac = lan_get_mac,
    .send = lan_send,
    .recv = lan_recv,
    .rx_pending = lan_rx_pending,
};

void netdev_register_usb_lan(void) {
    /* ready() is hard-wired to 0 — accidental install cannot go NETDEV_READY. */
    netdev_register(&lan_ops);
}
