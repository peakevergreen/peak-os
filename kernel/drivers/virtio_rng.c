#include "virtio_rng.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "random.h"
#include "util.h"
#include "serial.h"

/* VirtIO entropy (legacy PCI transitional) — QEMU virtio-rng-pci-transitional.
 * Vendor 1AF4 / device 1004. Seeds CSPRNG when boot entropy was weak.
 * Legacy config is an I/O BAR — use in/out, not MMIO pointer stores. */

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_RNG_DEV 0x1005 /* legacy transitional RNG (QEMU/Linux ID) */

#define VIRTIO_ACKNOWLEDGE 1
#define VIRTIO_DRIVER 2
#define VIRTIO_DRIVER_OK 4

#define QNUM 8
#define BUF_SZ 64
#define VRING_DESC_F_WRITE 2

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

static uint16_t iobase;
static int seeded;

static void outb_port(uint16_t port, uint8_t v) {
    __asm__ volatile("outb %0, %w1" : : "a"(v), "Nd"(port));
}
static void outw_port(uint16_t port, uint16_t v) {
    __asm__ volatile("outw %0, %w1" : : "a"(v), "Nd"(port));
}
static void outl_port(uint16_t port, uint32_t v) {
    __asm__ volatile("outl %0, %w1" : : "a"(v), "Nd"(port));
}
static uint16_t inw_port(uint16_t port) {
    uint16_t v;
    __asm__ volatile("inw %w1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static uint32_t inl_port(uint16_t port) {
    uint32_t v;
    __asm__ volatile("inl %w1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void viow8(uint16_t off, uint8_t v) { outb_port((uint16_t)(iobase + off), v); }
static void viow16(uint16_t off, uint16_t v) { outw_port((uint16_t)(iobase + off), v); }
static void viow32(uint16_t off, uint32_t v) { outl_port((uint16_t)(iobase + off), v); }
static uint16_t vior16(uint16_t off) { return inw_port((uint16_t)(iobase + off)); }
static uint32_t vior32(uint16_t off) { return inl_port((uint16_t)(iobase + off)); }

static void *cpu_map(void *phys) {
    return (void *)(uintptr_t)((uint64_t)(uintptr_t)phys + pmm_hhdm());
}

int virtio_rng_seeded(void) {
    return seeded;
}

int virtio_rng_init(void) {
    seeded = 0;
    iobase = 0;
    if (random_ready(RANDOM_DOMAIN_CRYPTO))
        return 0;

    struct pci_device dev;
    if (pci_find(VIRTIO_VENDOR, VIRTIO_RNG_DEV, &dev) != 0 &&
        pci_find(VIRTIO_VENDOR, 0x1004, &dev) != 0 &&
        pci_find(VIRTIO_VENDOR, 0x1003, &dev) != 0) {
        serial_log(SERIAL_LOG_WARN, "virtio-rng: PCI device not found\n");
        return -1;
    }
    pci_enable_bus_master(&dev);
    uint32_t bar = dev.bar0;
    if (!(bar & 1)) {
        serial_log(SERIAL_LOG_WARN, "virtio-rng: need legacy I/O BAR\n");
        return -1;
    }
    iobase = (uint16_t)(bar & ~3u);

    viow8(18, 0);
    viow8(18, VIRTIO_ACKNOWLEDGE);
    viow8(18, VIRTIO_ACKNOWLEDGE | VIRTIO_DRIVER);
    (void)vior32(0);
    viow32(4, 0);

    viow16(14, 0);
    uint16_t qsz = vior16(12);
    if (qsz < QNUM) {
        serial_log(SERIAL_LOG_WARN, "virtio-rng: queue too small\n");
        return -1;
    }

    void *ringp = pmm_alloc_pages(2); /* desc+avail page0, used page1 (legacy contiguous) */
    void *bp = pmm_alloc_pages(1);
    if (!ringp || !bp)
        return -1;
    memset(cpu_map(ringp), 0, 8192);
    memset(cpu_map(bp), 0, 4096);

    struct vring_desc *desc = (struct vring_desc *)cpu_map(ringp);
    struct vring_avail *avail =
        (struct vring_avail *)((uint8_t *)cpu_map(ringp) + QNUM * 16);
    struct vring_used *used =
        (struct vring_used *)((uint8_t *)cpu_map(ringp) + 4096);
    uint8_t *bufs = (uint8_t *)cpu_map(bp);
    uint64_t bufs_phys = (uint64_t)(uintptr_t)bp;

    /* Legacy QueuePFN = first page of contiguous desc/avail/used region. */
    viow32(8, (uint32_t)((uintptr_t)ringp >> 12));

    for (uint16_t i = 0; i < QNUM; i++) {
        desc[i].addr = bufs_phys + (uint64_t)i * BUF_SZ;
        desc[i].len = BUF_SZ;
        desc[i].flags = VRING_DESC_F_WRITE;
        desc[i].next = 0;
        avail->ring[i] = i;
    }
    __asm__ volatile("" ::: "memory");
    avail->idx = QNUM;

    viow8(18, VIRTIO_ACKNOWLEDGE | VIRTIO_DRIVER | VIRTIO_DRIVER_OK);
    viow16(16, 0); /* QueueNotify queue 0 */

    uint16_t last = 0;
    uint8_t collected[256];
    size_t got = 0;
    /* platform_init runs before arch_irq_enable — do not wait on timer_ticks. */
    for (int spins = 0; got < sizeof(collected) && spins < 2000000; spins++) {
        if (last == used->idx) {
            __asm__ volatile("pause");
            continue;
        }
        struct vring_used_elem *e = &used->ring[last % QNUM];
        uint16_t id = (uint16_t)e->id;
        uint32_t len = e->len;
        last++;
        if (len > BUF_SZ)
            len = BUF_SZ;
        if (len > 0 && got < sizeof(collected)) {
            size_t n = len;
            if (n > sizeof(collected) - got)
                n = sizeof(collected) - got;
            memcpy(collected + got, bufs + id * BUF_SZ, n);
            got += n;
        }
        desc[id].addr = bufs_phys + (uint64_t)id * BUF_SZ;
        desc[id].len = BUF_SZ;
        desc[id].flags = VRING_DESC_F_WRITE;
        avail->ring[avail->idx % QNUM] = id;
        __asm__ volatile("" ::: "memory");
        avail->idx++;
        viow16(16, 0);
    }

    if (got < 32) {
        memzero_explicit(collected, sizeof(collected));
        serial_log(SERIAL_LOG_WARN, "virtio-rng: insufficient entropy\n");
        return -1;
    }

    random_absorb_trusted(collected, got);
    memzero_explicit(collected, sizeof(collected));
    seeded = 1;
    serial_log(SERIAL_LOG_INFO, "virtio-rng: crypto entropy ready\n");
    return 0;
}
