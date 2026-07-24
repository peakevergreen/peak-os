#include "netdev.h"
#include "serial.h"
#include "util.h"
#include "rpi.h"

/* Broadcom GENET v5 — Pi 4 / CM4 / 400.
 * Staged stub: rings + PHY (MDIO) are not implemented. Never claims ready.
 * Platform must not call netdev_register_genet until NETDEV_READY is reachable. */

static uint8_t mac[6] = { 0xdc, 0xa6, 0x32, 0x00, 0x00, 0x01 };

static int genet_init(void) {
    if (rpi_get()->soc != RPI_SOC_BCM2711) {
        serial_write_str("genet: wrong SoC (stub)\n");
        return -1;
    }
    serial_write_str("genet: stub (not ready; rings/PHY deferred)\n");
    return -1;
}

static int genet_ready(void) { return 0; }
static void genet_mac(uint8_t m[6]) { memcpy(m, mac, 6); }
static int genet_send(const void *d, uint16_t l) { (void)d; (void)l; return -1; }
static int genet_recv(void *b, uint16_t c) { (void)b; (void)c; return -1; }
static int genet_rx(void) { return 0; }

static const struct netdev_ops genet_ops = {
    .name = "genet",
    .init = genet_init,
    .ready = genet_ready,
    .get_mac = genet_mac,
    .send = genet_send,
    .recv = genet_recv,
    .rx_pending = genet_rx,
};

void netdev_register_genet(void) {
    /* Keep ops linked for future bring-up; platform must not call this yet.
     * ready() is hard-wired to 0 so an accidental install cannot go NETDEV_READY. */
    netdev_register(&genet_ops);
}
