#include "netdev.h"
#include "serial.h"
#include "util.h"
#include "rpi.h"
#include "platform.h"
#include "pmm.h"

/* Broadcom GENET v5 — Pi 4 / CM4 / 400 (BCM2711).
 * Rings + MDIO probe; ready only when SYS_REV looks like GENET and PHY ID
 * reads non-zero. Never ready on Pi 3 / wrong SoC. */

#define GENET_OFF 0x580000ULL
#define SYS_REV_CTRL 0x0020
#define UMAC_CMD     0x0808
#define UMAC_MAC0    0x080c
#define UMAC_MAC1    0x0810
#define MDIO_CMD     0x0614
#define RDMA_RING_CFG 0x2000
#define TDMA_RING_CFG 0x4000

#define GENET_DESC_COUNT 16
#define GENET_BUF_SIZE   2048

struct genet_desc {
    uint32_t len_status;
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t next;
};

static uint8_t mac[6] = { 0xdc, 0xa6, 0x32, 0x00, 0x00, 0x01 };
static volatile uint32_t *regs;
static int ready_flag;
static struct genet_desc *rx_ring;
static struct genet_desc *tx_ring;
static uint8_t *rx_bufs;
static uint8_t *tx_bufs;
static unsigned rx_c, tx_c;

static uint32_t gr(uint32_t off) { return regs[off / 4]; }
static void gw(uint32_t off, uint32_t v) { regs[off / 4] = v; }

static int mdio_read(uint8_t phy, uint8_t reg, uint16_t *out) {
    uint32_t cmd = (1u << 30) | ((uint32_t)phy << 21) | ((uint32_t)reg << 16);
    gw(MDIO_CMD, cmd);
    for (int i = 0; i < 10000; i++) {
        uint32_t v = gr(MDIO_CMD);
        if (!(v & (1u << 29))) {
            *out = (uint16_t)(v & 0xffff);
            return 0;
        }
    }
    return -1;
}

static void *phys_to_cpu(void *phys) {
    return (void *)(uintptr_t)((uint64_t)(uintptr_t)phys + pmm_hhdm());
}

static int genet_init(void) {
    ready_flag = 0;
    if (rpi_get()->soc != RPI_SOC_BCM2711) {
        serial_log(SERIAL_LOG_DEBUG, "genet: wrong SoC (not Pi 4)\n");
        return -1;
    }
    if (!platform_mmio_mapped()) {
        serial_log(SERIAL_LOG_DEBUG, "genet: MMIO not mapped\n");
        return -1;
    }
    uint64_t base = rpi_get()->peri_base + GENET_OFF;
    regs = (volatile uint32_t *)(uintptr_t)base;

    uint32_t rev = gr(SYS_REV_CTRL);
    if (rev == 0 || rev == 0xffffffffu) {
        serial_log(SERIAL_LOG_DEBUG, "genet: SYS_REV invalid (not ready)\n");
        return -1;
    }

    uint16_t phyid = 0;
    if (mdio_read(1, 2, &phyid) != 0 || phyid == 0 || phyid == 0xffff) {
        serial_log(SERIAL_LOG_DEBUG, "genet: PHY ID read failed (not ready)\n");
        return -1;
    }

    void *rxp = pmm_alloc_pages(2);
    void *txp = pmm_alloc_pages(2);
    void *rbp = pmm_alloc_pages(8);
    void *tbp = pmm_alloc_pages(8);
    if (!rxp || !txp || !rbp || !tbp) {
        serial_log(SERIAL_LOG_WARN, "genet: ring alloc failed\n");
        return -1;
    }
    rx_ring = (struct genet_desc *)phys_to_cpu(rxp);
    tx_ring = (struct genet_desc *)phys_to_cpu(txp);
    rx_bufs = (uint8_t *)phys_to_cpu(rbp);
    tx_bufs = (uint8_t *)phys_to_cpu(tbp);
    memset(rx_ring, 0, sizeof(struct genet_desc) * GENET_DESC_COUNT);
    memset(tx_ring, 0, sizeof(struct genet_desc) * GENET_DESC_COUNT);

    for (unsigned i = 0; i < GENET_DESC_COUNT; i++) {
        uint64_t ba = (uint64_t)(uintptr_t)rbp + (uint64_t)i * GENET_BUF_SIZE;
        rx_ring[i].addr_lo = (uint32_t)ba;
        rx_ring[i].addr_hi = (uint32_t)(ba >> 32);
        rx_ring[i].len_status = GENET_BUF_SIZE;
        ba = (uint64_t)(uintptr_t)tbp + (uint64_t)i * GENET_BUF_SIZE;
        tx_ring[i].addr_lo = (uint32_t)ba;
        tx_ring[i].addr_hi = (uint32_t)(ba >> 32);
    }
    rx_c = tx_c = 0;

    uint32_t m0 = gr(UMAC_MAC0);
    uint32_t m1 = gr(UMAC_MAC1);
    if (m0 || m1) {
        mac[0] = (uint8_t)(m0 >> 8);
        mac[1] = (uint8_t)(m0);
        mac[2] = (uint8_t)(m1 >> 24);
        mac[3] = (uint8_t)(m1 >> 16);
        mac[4] = (uint8_t)(m1 >> 8);
        mac[5] = (uint8_t)(m1);
    }

    gw(RDMA_RING_CFG, (uint32_t)(uintptr_t)rxp);
    gw(TDMA_RING_CFG, (uint32_t)(uintptr_t)txp);
    gw(UMAC_CMD, gr(UMAC_CMD) | 0x3);

    ready_flag = 1;
    char msg[80];
    snprintf(msg, sizeof(msg),
             "genet: rings+PHY ready rev=0x%x phy=0x%04x\n", rev, phyid);
    serial_log(SERIAL_LOG_INFO, msg);
    return 0;
}

static int genet_ready(void) { return ready_flag; }
static void genet_mac(uint8_t m[6]) { memcpy(m, mac, 6); }

static int genet_send(const void *d, uint16_t l) {
    if (!ready_flag || !d || l < 14 || l > 1514)
        return -1;
    struct genet_desc *td = &tx_ring[tx_c % GENET_DESC_COUNT];
    uint8_t *buf = tx_bufs + (tx_c % GENET_DESC_COUNT) * GENET_BUF_SIZE;
    memcpy(buf, d, l);
    td->len_status = l | (1u << 15); /* OWN */
    tx_c++;
    return 0;
}

static int genet_recv(void *b, uint16_t c) {
    if (!ready_flag || !b || !c)
        return -1;
    struct genet_desc *rd = &rx_ring[rx_c % GENET_DESC_COUNT];
    uint32_t ls = rd->len_status;
    if (ls & (1u << 15)) /* still OWN by hardware */
        return -1;
    uint16_t n = (uint16_t)(ls & 0xfff);
    if (n < 14 || n > 1514)
        return -1;
    if (n > c)
        n = c;
    memcpy(b, rx_bufs + (rx_c % GENET_DESC_COUNT) * GENET_BUF_SIZE, n);
    rd->len_status = GENET_BUF_SIZE | (1u << 15);
    rx_c++;
    return (int)n;
}

static int genet_rx(void) {
    if (!ready_flag)
        return 0;
    return (rx_ring[rx_c % GENET_DESC_COUNT].len_status & (1u << 15)) ? 0 : 1;
}

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
