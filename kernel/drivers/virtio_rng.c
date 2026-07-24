#include "virtio_rng.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "random.h"
#include "util.h"
#include "serial.h"
#include "timer.h"

/* VirtIO entropy (legacy PCI transitional) — QEMU -device virtio-rng-pci.
 * Vendor 1AF4 / device 1004. Seeds CSPRNG when boot entropy was weak. */

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_RNG_DEV 0x1004

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

static volatile uint8_t *io;
static int seeded;

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
static uint32_t vior32(uint16_t off) {
    return (uint32_t)io[off] | ((uint32_t)io[off + 1] << 8) |
           ((uint32_t)io[off + 2] << 16) | ((uint32_t)io[off + 3] << 24);
}

static void *cpu_map(void *phys) {
    return (void *)(uintptr_t)((uint64_t)(uintptr_t)phys + pmm_hhdm());
}

int virtio_rng_seeded(void) {
    return seeded;
}

int virtio_rng_init(void) {
    seeded = 0;
    if (random_ready(RANDOM_DOMAIN_CRYPTO))
        return 0; /* already have trusted entropy */

    struct pci_device dev;
    if (pci_find(VIRTIO_VENDOR, VIRTIO_RNG_DEV, &dev) != 0)
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
    (void)vior32(0);
    viow32(4, 0); /* no feature bits required */

    viow16(14, 0);
    uint16_t qsz = (uint16_t)(io[12] | (io[13] << 8));
    if (qsz < QNUM)
        return -1;

    void *dp = pmm_alloc_pages(2);
    void *up = pmm_alloc_pages(1);
    void *bp = pmm_alloc_pages(1);
    if (!dp || !up || !bp)
        return -1;
    memset(cpu_map(dp), 0, 8192);
    memset(cpu_map(up), 0, 4096);
    memset(cpu_map(bp), 0, 4096);

    struct vring_desc *desc = (struct vring_desc *)cpu_map(dp);
    struct vring_avail *avail = (struct vring_avail *)((uint8_t *)cpu_map(dp) + QNUM * 16);
    struct vring_used *used = (struct vring_used *)cpu_map(up);
    uint8_t *bufs = (uint8_t *)cpu_map(bp);
    uint64_t bufs_phys = (uint64_t)(uintptr_t)bp;

    viow32(8, (uint32_t)((uintptr_t)dp >> 12));

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
    viow16(16, 0); /* notify queue 0 */

    uint16_t last = 0;
    uint64_t start = timer_ticks();
    uint8_t collected[256];
    size_t got = 0;
    while (got < sizeof(collected) && timer_ticks() - start < 200) {
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
        /* re-queue */
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
