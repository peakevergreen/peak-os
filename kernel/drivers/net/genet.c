#include "netdev.h"
#include "serial.h"
#include "util.h"
#include "rpi.h"

/* Broadcom GENET v5 — Pi 4 / CM4 / 400.
 * MMIO base from DT (typically 0xfd580000). Init enables UMAC and sets MAC. */

#define GENET_SYS_REV_CTRL   0x000
#define GENET_SYS_PORT_CTRL  0x004
#define GENET_UMAC_CMD       0x808
#define GENET_UMAC_MAC0      0x80c
#define GENET_UMAC_MAC1      0x810

static uint8_t mac[6] = { 0xdc, 0xa6, 0x32, 0x00, 0x00, 0x01 };
static int ready;
static volatile uint32_t *genet;

static int genet_init(void) {
    struct rpi_plat *p = rpi_get();
    uint64_t base = 0xfd580000ULL;
    if (p->soc != RPI_SOC_BCM2711) {
        serial_write_str("genet: wrong SoC\n");
        return -1;
    }
    genet = (volatile uint32_t *)(uintptr_t)base;
    uint32_t rev = genet[GENET_SYS_REV_CTRL / 4];
    serial_write_str("genet: rev=");
    char b[12];
    itoa_u(rev, b, 16);
    serial_write_str(b);
    serial_write_str("\n");

    /* Program default MAC; real boards override from DT local-mac-address. */
    genet[GENET_UMAC_MAC0 / 4] = ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
                                 ((uint32_t)mac[2] << 8) | mac[3];
    genet[GENET_UMAC_MAC1 / 4] = ((uint32_t)mac[4] << 8) | mac[5];

    /* Full TX/RX ring setup + PHY (MDIO) required for traffic. */
    ready = 0;
    serial_write_str("genet: probed (rings/PHY pending)\n");
    return -1;
}

static int genet_ready(void) { return ready; }
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
    netdev_register(&genet_ops);
}
