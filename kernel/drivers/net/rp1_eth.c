#include "netdev.h"
#include "serial.h"
#include "util.h"
#include "rpi.h"

/* RP1 Gigabit Ethernet MAC (GEM) on Pi 5 / 500 / CM5 — behind PCIe. */

static uint8_t mac[6] = { 0x2c, 0xcf, 0x67, 0x00, 0x00, 0x01 };
static int ready;

static int rp1_init(void) {
    if (rpi_get()->soc != RPI_SOC_BCM2712) {
        serial_write_str("rp1-eth: wrong SoC\n");
        return -1;
    }
    serial_write_str("rp1-eth: GEM behind RP1 PCIe\n");
    ready = 0;
    return -1;
}

static int rp1_ready(void) { return ready; }
static void rp1_mac(uint8_t m[6]) { memcpy(m, mac, 6); }
static int rp1_send(const void *d, uint16_t l) { (void)d; (void)l; return -1; }
static int rp1_recv(void *b, uint16_t c) { (void)b; (void)c; return -1; }
static int rp1_rx(void) { return 0; }

static const struct netdev_ops rp1_ops = {
    .name = "rp1-gem",
    .init = rp1_init,
    .ready = rp1_ready,
    .get_mac = rp1_mac,
    .send = rp1_send,
    .recv = rp1_recv,
    .rx_pending = rp1_rx,
};

void netdev_register_rp1_eth(void) {
    netdev_register(&rp1_ops);
}
