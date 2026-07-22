#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "util.h"
#include "serial.h"

/* Intel e1000 (82540EM) — QEMU default with -device e1000 */

#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_EECD     0x0010
#define E1000_REG_EERD     0x0014
#define E1000_REG_ICR      0x00C0
#define E1000_REG_IMS      0x00D0
#define E1000_REG_IMC      0x00D8
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TCTL     0x0400
#define E1000_REG_TIPG     0x0410
#define E1000_REG_RDBAL    0x2800
#define E1000_REG_RDBAH    0x2804
#define E1000_REG_RDLEN    0x2808
#define E1000_REG_RDH      0x2810
#define E1000_REG_RDT      0x2818
#define E1000_REG_TDBAL    0x3800
#define E1000_REG_TDBAH    0x3804
#define E1000_REG_TDLEN    0x3808
#define E1000_REG_TDH      0x3810
#define E1000_REG_TDT      0x3818
#define E1000_REG_MTA      0x5200
#define E1000_REG_RAL      0x5400
#define E1000_REG_RAH      0x5404

#define RCTL_EN            (1u << 1)
#define RCTL_SBP           (1u << 2)
#define RCTL_UPE           (1u << 3)
#define RCTL_MPE           (1u << 4)
#define RCTL_LPE           (1u << 5)
#define RCTL_LBM_NONE      (0u << 6)
#define RCTL_BAM           (1u << 15)
#define RCTL_BSIZE_2048    (0u << 16)
#define RCTL_SECRC         (1u << 26)

#define TCTL_EN            (1u << 1)
#define TCTL_PSP           (1u << 3)
#define TCTL_CT_SHIFT      4
#define TCTL_COLD_SHIFT    12

#define CMD_EOP            (1u << 0)
#define CMD_IFCS           (1u << 1)
#define CMD_RS             (1u << 3)

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 8
#define RX_BUF_SIZE   2048

struct e1000_rx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed));

static volatile uint32_t *mmio;
static uint8_t mac_addr[6];
static int ready;

static struct e1000_rx_desc *rx_descs;
static struct e1000_tx_desc *tx_descs;
static uint8_t *rx_bufs[RX_DESC_COUNT];
static uint8_t *tx_bufs[TX_DESC_COUNT];
static uint32_t rx_tail;
static uint32_t tx_tail;
static uint64_t stat_rx_packets, stat_tx_packets;
static uint64_t stat_rx_bytes, stat_tx_bytes;

static uint32_t mmio_read(uint32_t reg) {
    return mmio[reg / 4];
}

static void mmio_write(uint32_t reg, uint32_t val) {
    mmio[reg / 4] = val;
}

static void *alloc_dma_page(uint64_t *phys_out) {
    void *phys = pmm_alloc();
    if (!phys)
        return NULL;
    *phys_out = (uint64_t)phys;
    void *virt = vmm_phys_to_virt((uint64_t)phys);
    memset(virt, 0, 4096);
    return virt;
}

static void read_mac(void) {
    uint32_t ral = mmio_read(E1000_REG_RAL);
    uint32_t rah = mmio_read(E1000_REG_RAH);
    if (ral || rah) {
        mac_addr[0] = (uint8_t)(ral & 0xFF);
        mac_addr[1] = (uint8_t)((ral >> 8) & 0xFF);
        mac_addr[2] = (uint8_t)((ral >> 16) & 0xFF);
        mac_addr[3] = (uint8_t)((ral >> 24) & 0xFF);
        mac_addr[4] = (uint8_t)(rah & 0xFF);
        mac_addr[5] = (uint8_t)((rah >> 8) & 0xFF);
        return;
    }
    /* EEPROM read fallback — QEMU usually fills RAL */
    mac_addr[0] = 0x52;
    mac_addr[1] = 0x54;
    mac_addr[2] = 0x00;
    mac_addr[3] = 0x12;
    mac_addr[4] = 0x34;
    mac_addr[5] = 0x56;
}

int e1000_init(void) {
    struct pci_device dev;
    /* 82540EM and variants commonly used by QEMU */
    if (pci_find(0x8086, 0x100E, &dev) != 0 &&
        pci_find(0x8086, 0x100F, &dev) != 0 &&
        pci_find(0x8086, 0x10D3, &dev) != 0) {
        serial_write_str("e1000: no NIC found\n");
        return -1;
    }

    pci_enable_bus_master(&dev);
    uint32_t bar = dev.bar0 & ~0xFu;
    mmio = (volatile uint32_t *)vmm_phys_to_virt(bar);

    /* Reset */
    mmio_write(E1000_REG_IMC, 0xFFFFFFFF);
    mmio_write(E1000_REG_CTRL, mmio_read(E1000_REG_CTRL) | (1u << 26));
    for (volatile int i = 0; i < 100000; i++)
        ;
    mmio_write(E1000_REG_IMC, 0xFFFFFFFF);
    (void)mmio_read(E1000_REG_ICR);

    read_mac();

    /* Clear multicast table */
    for (uint32_t i = 0; i < 128; i++)
        mmio_write(E1000_REG_MTA + i * 4, 0);

    uint64_t rx_phys = 0, tx_phys = 0;
    rx_descs = alloc_dma_page(&rx_phys);
    tx_descs = alloc_dma_page(&tx_phys);
    if (!rx_descs || !tx_descs) {
        serial_write_str("e1000: DMA descriptor alloc failed\n");
        return -1;
    }

    for (int i = 0; i < RX_DESC_COUNT; i++) {
        uint64_t p = 0;
        rx_bufs[i] = alloc_dma_page(&p);
        if (!rx_bufs[i]) {
            serial_write_str("e1000: RX buffer alloc failed\n");
            return -1;
        }
        rx_descs[i].addr = p;
        rx_descs[i].status = 0;
    }
    for (int i = 0; i < TX_DESC_COUNT; i++) {
        uint64_t p = 0;
        tx_bufs[i] = alloc_dma_page(&p);
        if (!tx_bufs[i]) {
            serial_write_str("e1000: TX buffer alloc failed\n");
            return -1;
        }
        tx_descs[i].addr = p;
        tx_descs[i].status = 1; /* DD — free */
    }

    mmio_write(E1000_REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFFu));
    mmio_write(E1000_REG_RDBAH, (uint32_t)(rx_phys >> 32));
    mmio_write(E1000_REG_RDLEN, RX_DESC_COUNT * 16);
    mmio_write(E1000_REG_RDH, 0);
    rx_tail = RX_DESC_COUNT - 1;
    mmio_write(E1000_REG_RDT, rx_tail);

    mmio_write(E1000_REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFFu));
    mmio_write(E1000_REG_TDBAH, (uint32_t)(tx_phys >> 32));
    mmio_write(E1000_REG_TDLEN, TX_DESC_COUNT * 16);
    mmio_write(E1000_REG_TDH, 0);
    tx_tail = 0;
    mmio_write(E1000_REG_TDT, 0);

    mmio_write(E1000_REG_RAL, (uint32_t)mac_addr[0] | ((uint32_t)mac_addr[1] << 8) |
                                  ((uint32_t)mac_addr[2] << 16) | ((uint32_t)mac_addr[3] << 24));
    mmio_write(E1000_REG_RAH, (uint32_t)mac_addr[4] | ((uint32_t)mac_addr[5] << 8) | (1u << 31));

    mmio_write(E1000_REG_RCTL, RCTL_EN | RCTL_UPE | RCTL_MPE | RCTL_LBM_NONE |
                                   RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);
    mmio_write(E1000_REG_TCTL, TCTL_EN | TCTL_PSP | (0x10u << TCTL_CT_SHIFT) |
                                   (0x40u << TCTL_COLD_SHIFT));
    mmio_write(E1000_REG_TIPG, 0x0060200A);

    /* Link up */
    mmio_write(E1000_REG_CTRL, mmio_read(E1000_REG_CTRL) | (1u << 6)); /* SLU */

    ready = 1;
    serial_write_str("e1000: up\n");
    return 0;
}

int e1000_ready(void) {
    return ready;
}

void e1000_get_mac(uint8_t mac[6]) {
    for (int i = 0; i < 6; i++)
        mac[i] = mac_addr[i];
}

int e1000_send(const void *data, uint16_t len) {
    if (!ready || len == 0 || len > 1518)
        return -1;
    struct e1000_tx_desc *d = &tx_descs[tx_tail];
    int spins = 100000;
    while (!(d->status & 1) && spins--)
        ;
    if (!(d->status & 1))
        return -1;
    memcpy(tx_bufs[tx_tail], data, len);
    d->length = len;
    d->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
    d->status = 0;
    tx_tail = (tx_tail + 1) % TX_DESC_COUNT;
    mmio_write(E1000_REG_TDT, tx_tail);
    stat_tx_packets++;
    stat_tx_bytes += len;
    return 0;
}

int e1000_rx_pending(void) {
    if (!ready || !rx_descs)
        return 0;
    uint32_t next = (rx_tail + 1) % RX_DESC_COUNT;
    return (rx_descs[next].status & 1) ? 1 : 0;
}

int e1000_recv(void *buf, uint16_t cap) {
    if (!ready)
        return -1;
    uint32_t next = (rx_tail + 1) % RX_DESC_COUNT;
    struct e1000_rx_desc *d = &rx_descs[next];
    if (!(d->status & 1))
        return -1;
    uint16_t len = d->length;
    if (len > cap)
        len = cap;
    memcpy(buf, rx_bufs[next], len);
    d->status = 0;
    rx_tail = next;
    mmio_write(E1000_REG_RDT, rx_tail);
    stat_rx_packets++;
    stat_rx_bytes += len;
    return (int)len;
}

void e1000_get_stats(struct e1000_stats *out) {
    if (!out)
        return;
    out->rx_packets = stat_rx_packets;
    out->tx_packets = stat_tx_packets;
    out->rx_bytes = stat_rx_bytes;
    out->tx_bytes = stat_tx_bytes;
}
