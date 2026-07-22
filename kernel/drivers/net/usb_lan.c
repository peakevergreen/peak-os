#include "netdev.h"
#include "serial.h"
#include "util.h"

/* LAN9514 (Pi 3B) / LAN7515–LAN7800 (Pi 3B+) USB Ethernet.
 * Depends on DWC2 host + hub enumeration. Bulk IN/OUT endpoints carry
 * Ethernet frames once the SMSC/Microchip class driver completes. */

static uint8_t mac[6] = { 0xb8, 0x27, 0xeb, 0x00, 0x00, 0x01 };
static int ready;

static int lan_init(void) {
    serial_write_str("usb-lan: SMSC/Microchip (after USB hub enum)\n");
    ready = 0;
    return -1;
}

static int lan_ready(void) { return ready; }
static void lan_get_mac(uint8_t m[6]) { memcpy(m, mac, 6); }
static int lan_send(const void *d, uint16_t l) {
    (void)d;
    (void)l;
    return ready ? -1 : -1;
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
    netdev_register(&lan_ops);
}
