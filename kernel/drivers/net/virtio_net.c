#include "netdev.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "util.h"
#include "serial.h"

/* VirtIO network (legacy PCI transitional) — preferred QEMU NIC.
 * Vendor 1AF4 / device 1000. Falls through if not present so e1000 remains. */

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_NET_DEV 0x1000

#define VIRTIO_ACKNOWLEDGE 1
#define VIRTIO_DRIVER 2
#define VIRTIO_DRIVER_OK 4

#define VIRTIO_NET_F_MAC (1u << 5)

#define QNUM 16
#define BUF_SZ 2048

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QNUM];
};
struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};
struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[QNUM];
};

static volatile uint8_t *io;
static int ready_flag;
static uint8_t mac[6];

static struct vring_desc *rx_desc, *tx_desc;
static struct vring_avail *rx_avail, *tx_avail;
static struct vring_used *rx_used, *tx_used;
static uint8_t *rx_bufs, *tx_bufs;
static uint16_t rx_last_used, tx_free;
static uint64_t rx_bufs_phys, tx_bufs_phys;

static void viow8(uint16_t off, uint8_t v) { io[off] = v; }
static void viow16(uint16_t off, uint16_t v) {
    io[off] = (uint8_t)v;
    io[off + 1] = (uint8_t)(v >> 8);
}
static void viow32(uint16_t off, uint32_t v) {
    io[off] = (uint8_t)v;
    io[off + 1] = (uint8_t)(v >> 8);
    io[off + 2] = (uint8_t)(v >> 16);
    io[off + 3] = (uint8_t)(v >> 24);
}
static uint8_t vior8(uint16_t off) { return io[off]; }
static uint32_t vior32(uint16_t off) {
    return (uint32_t)io[off] | ((uint32_t)io[off + 1] << 8) |
           ((uint32_t)io[off + 2] << 16) | ((uint32_t)io[off + 3] << 24);
}

static void *cpu_map(void *phys) {
    return (void *)(uintptr_t)((uint64_t)(uintptr_t)phys + pmm_hhdm());
}

static int setup_queue(int qidx, struct vring_desc **desc, struct vring_avail **avail,
                       struct vring_used **used) {
    viow16(14, (uint16_t)qidx);
    uint16_t qsz = (uint16_t)(io[12] | (io[13] << 8));
    if (qsz < QNUM)
        return -1;
    void *dp = pmm_alloc_pages(2);
    void *up = pmm_alloc_pages(1);
    if (!dp || !up)
        return -1;
    memset(cpu_map(dp), 0, 8192);
    memset(cpu_map(up), 0, 4096);
    *desc = (struct vring_desc *)cpu_map(dp);
    *avail = (struct vring_avail *)((uint8_t *)cpu_map(dp) + QNUM * 16);
    *used = (struct vring_used *)cpu_map(up);
    /* Legacy QueuePFN is physical address >> 12 */
    viow32(8, (uint32_t)((uintptr_t)dp >> 12));
    return 0;
}

static int virtio_net_init(void) {
    ready_flag = 0;
    struct pci_device dev;
    if (pci_find(VIRTIO_VENDOR, VIRTIO_NET_DEV, &dev) != 0)
        return -1;
    pci_enable_bus_master(&dev);
    uint32_t bar = dev.bar0;
    if (bar & 1)
        io = (volatile uint8_t *)(uintptr_t)(bar & ~3u);
    else
        io = (volatile uint8_t *)vmm_phys_to_virt(bar & ~0xfu);
    if (!io)
        return -1;

    viow8(18, 0);
    viow8(18, VIRTIO_ACKNOWLEDGE);
    viow8(18, VIRTIO_ACKNOWLEDGE | VIRTIO_DRIVER);

    uint32_t feats = vior32(0);
    feats &= VIRTIO_NET_F_MAC;
    viow32(4, feats);

    if (setup_queue(0, &rx_desc, &rx_avail, &rx_used) != 0)
        return -1;
    if (setup_queue(1, &tx_desc, &tx_avail, &tx_used) != 0)
        return -1;

    void *rbp = pmm_alloc_pages(8);
    void *tbp = pmm_alloc_pages(8);
    if (!rbp || !tbp)
        return -1;
    rx_bufs = (uint8_t *)cpu_map(rbp);
    tx_bufs = (uint8_t *)cpu_map(tbp);
    rx_bufs_phys = (uint64_t)(uintptr_t)rbp;
    tx_bufs_phys = (uint64_t)(uintptr_t)tbp;

    for (uint16_t i = 0; i < QNUM; i++) {
        rx_desc[i].addr = rx_bufs_phys + (uint64_t)i * BUF_SZ;
        rx_desc[i].len = BUF_SZ;
        rx_desc[i].flags = 2;
        rx_desc[i].next = 0;
        rx_avail->ring[i] = i;
    }
    rx_avail->idx = QNUM;
    rx_last_used = 0;
    tx_free = 0;

    if (feats & VIRTIO_NET_F_MAC) {
        for (int i = 0; i < 6; i++)
            mac[i] = vior8(0x14 + (uint16_t)i);
    } else {
        mac[0] = 0x52;
        mac[1] = 0x54;
        mac[2] = 0x00;
        mac[3] = 0x12;
        mac[4] = 0x34;
        mac[5] = 0x56;
    }

    viow8(18, VIRTIO_ACKNOWLEDGE | VIRTIO_DRIVER | VIRTIO_DRIVER_OK);
    viow16(16, 0);
    ready_flag = 1;
    serial_log(SERIAL_LOG_INFO, "virtio-net: ready\n");
    return 0;
}

static int virtio_net_ready(void) { return ready_flag; }
static void virtio_net_mac(uint8_t m[6]) { memcpy(m, mac, 6); }

static int virtio_net_send(const void *d, uint16_t l) {
    if (!ready_flag || !d || l < 14 || l > 1514)
        return -1;
    uint16_t i = tx_free % QNUM;
    uint8_t *buf = tx_bufs + i * BUF_SZ;
    memset(buf, 0, 10);
    memcpy(buf + 10, d, l);
    tx_desc[i].addr = tx_bufs_phys + (uint64_t)i * BUF_SZ;
    tx_desc[i].len = 10 + l;
    tx_desc[i].flags = 0;
    tx_avail->ring[tx_avail->idx % QNUM] = i;
    __asm__ volatile("" ::: "memory");
    tx_avail->idx++;
    viow16(16, 1);
    tx_free++;
    return 0;
}

static int virtio_net_recv(void *b, uint16_t c) {
    if (!ready_flag || !b || !c)
        return -1;
    if (rx_last_used == rx_used->idx)
        return -1;
    struct vring_used_elem *e = &rx_used->ring[rx_last_used % QNUM];
    uint16_t id = (uint16_t)e->id;
    uint32_t len = e->len;
    rx_last_used++;
    int ret = -1;
    if (len > 10) {
        uint16_t n = (uint16_t)(len - 10);
        if (n > c)
            n = c;
        memcpy(b, rx_bufs + id * BUF_SZ + 10, n);
        ret = (int)n;
    }
    rx_avail->ring[rx_avail->idx % QNUM] = id;
    __asm__ volatile("" ::: "memory");
    rx_avail->idx++;
    viow16(16, 0);
    return ret;
}

static int virtio_net_rx_pending(void) {
    return ready_flag && rx_last_used != rx_used->idx;
}

static const struct netdev_ops virtio_ops = {
    .name = "virtio-net",
    .init = virtio_net_init,
    .ready = virtio_net_ready,
    .get_mac = virtio_net_mac,
    .send = virtio_net_send,
    .recv = virtio_net_recv,
    .rx_pending = virtio_net_rx_pending,
};

void netdev_register_virtio_net(void) {
    netdev_register(&virtio_ops);
}
