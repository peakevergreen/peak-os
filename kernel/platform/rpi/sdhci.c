#include "rpi.h"
#include "blockdev.h"
#include "serial.h"
#include "util.h"

/* Arasan SDHCI (BCM283x / 2711) — PIO single/multi-block for PeakFS.
 * After card init, MBR is scanned for the PeakFS partition (type 0x83);
 * blockdev LBAs are relative to that partition. */

static int sdhci_ok;
static int card_ready;
static uint32_t rca;
static uint64_t part_lba0;
static uint64_t part_count;
static volatile uint32_t *sd;

#define SD_BLKSIZECNT 0x04
#define SD_ARG        0x08
#define SD_CMDTM      0x0c
#define SD_RESP0      0x10
#define SD_DATA       0x20
#define SD_STATUS     0x24
#define SD_CONTROL0   0x28
#define SD_CONTROL1   0x2c
#define SD_INTERRUPT  0x30
#define SD_IRPT_MASK  0x34
#define SD_IRPT_EN    0x38

#define INT_CMD_DONE  (1u << 0)
#define INT_DATA_DONE (1u << 1)
#define INT_WRITE_RDY (1u << 4)
#define INT_READ_RDY  (1u << 5)
#define INT_ERROR     (1u << 15)

#define CMD_ISDATA    (1u << 21)
#define CMD_RSP136    (2u << 16)
#define CMD_RSP48     (1u << 16)
#define CMD_RSP48B    (3u << 16)
#define CMD_CRC       (1u << 19)
#define CMD_IXCHK     (1u << 20)
#define CMD_MULTI     (1u << 5)
#define CMD_READ      (1u << 4)

static void sd_delay(void) {
    for (volatile int i = 0; i < 200; i++)
        ;
}

static void sd_wait_idle(void) {
    for (int i = 0; i < 1000000; i++) {
        if (!(sd[SD_STATUS / 4] & 0x3))
            return;
    }
}

static int sd_wait_int(uint32_t mask) {
    for (int i = 0; i < 2000000; i++) {
        uint32_t st = sd[SD_INTERRUPT / 4];
        if (st & INT_ERROR) {
            sd[SD_INTERRUPT / 4] = st;
            return -1;
        }
        if ((st & mask) == mask) {
            sd[SD_INTERRUPT / 4] = mask;
            return 0;
        }
    }
    return -1;
}

static int sd_cmd(uint32_t index, uint32_t arg, uint32_t flags, uint32_t *resp) {
    sd_wait_idle();
    sd[SD_INTERRUPT / 4] = 0xffffffffu;
    sd[SD_ARG / 4] = arg;
    sd[SD_CMDTM / 4] = (index << 24) | flags;
    if (sd_wait_int(INT_CMD_DONE) != 0)
        return -1;
    if (resp) {
        resp[0] = sd[SD_RESP0 / 4];
        resp[1] = sd[SD_RESP0 / 4 + 1];
        resp[2] = sd[SD_RESP0 / 4 + 2];
        resp[3] = sd[SD_RESP0 / 4 + 3];
    }
    return 0;
}

static int sd_set_clock(int div) {
    uint32_t c = sd[SD_CONTROL1 / 4];
    c &= ~0xffe0u;
    c |= ((uint32_t)div & 0xff) << 8;
    c |= 1u; /* clock enable */
    sd[SD_CONTROL1 / 4] = c;
    for (int i = 0; i < 10000; i++) {
        if (sd[SD_CONTROL1 / 4] & (1u << 1))
            break;
        sd_delay();
    }
    return 0;
}

static int sd_card_init(void) {
    uint32_t resp[4];
    sd[SD_CONTROL0 / 4] = 0;
    sd[SD_CONTROL1 / 4] = (1u << 24); /* SRST_HC */
    for (int i = 0; i < 100000; i++) {
        if (!(sd[SD_CONTROL1 / 4] & (1u << 24)))
            break;
    }
    sd[SD_CONTROL1 / 4] = (1u << 24) | (7u << 16); /* timeout */
    sd_delay();
    sd[SD_CONTROL1 / 4] = (7u << 16);
    sd[SD_IRPT_EN / 4] = 0xffffffffu;
    sd[SD_IRPT_MASK / 4] = 0xffffffffu;
    sd_set_clock(0x80); /* slow ident */

    if (sd_cmd(0, 0, 0, 0) != 0)
        return -1;
    if (sd_cmd(8, 0x1AA, CMD_RSP48 | CMD_CRC | CMD_IXCHK, resp) != 0)
        return -1;

    for (int i = 0; i < 1000; i++) {
        if (sd_cmd(55, 0, CMD_RSP48 | CMD_CRC | CMD_IXCHK, resp) != 0)
            return -1;
        if (sd_cmd(41, 0x40FF8000, CMD_RSP48, resp) != 0)
            return -1;
        if (resp[0] & (1u << 31))
            break;
        sd_delay();
    }
    if (!(resp[0] & (1u << 31)))
        return -1;

    if (sd_cmd(2, 0, CMD_RSP136 | CMD_CRC, resp) != 0)
        return -1;
    if (sd_cmd(3, 0, CMD_RSP48 | CMD_CRC | CMD_IXCHK, resp) != 0)
        return -1;
    rca = resp[0] & 0xffff0000u;
    if (sd_cmd(7, rca, CMD_RSP48B | CMD_CRC | CMD_IXCHK, resp) != 0)
        return -1;

    sd_set_clock(8); /* faster transfer */
    sd[SD_CONTROL0 / 4] = (1u << 1); /* 4-bit bus after ACMD6 */
    if (sd_cmd(55, rca, CMD_RSP48 | CMD_CRC | CMD_IXCHK, resp) == 0)
        (void)sd_cmd(6, 2, CMD_RSP48 | CMD_CRC | CMD_IXCHK, resp);

    card_ready = 1;
    return 0;
}

static int sd_xfer(uint64_t lba, uint32_t count, void *buf, int write) {
    if (!card_ready || !buf || count == 0 || count > 65535)
        return -1;
    uint32_t resp[4];
    uint32_t *p = (uint32_t *)buf;
    uint32_t cmd = write ? 25 : 18; /* multi-block */
    if (count == 1)
        cmd = write ? 24 : 17;

    sd[SD_BLKSIZECNT / 4] = (count << 16) | 512;
    uint32_t flags = CMD_ISDATA | CMD_RSP48 | CMD_CRC | CMD_IXCHK;
    if (!write)
        flags |= CMD_READ;
    if (count > 1)
        flags |= CMD_MULTI;

    if (sd_cmd(cmd, (uint32_t)lba, flags, resp) != 0)
        return -1;

    for (uint32_t s = 0; s < count; s++) {
        if (write) {
            if (sd_wait_int(INT_WRITE_RDY) != 0)
                return -1;
            for (int i = 0; i < 128; i++)
                sd[SD_DATA / 4] = *p++;
        } else {
            if (sd_wait_int(INT_READ_RDY) != 0)
                return -1;
            for (int i = 0; i < 128; i++)
                *p++ = sd[SD_DATA / 4];
        }
    }
    if (sd_wait_int(INT_DATA_DONE) != 0)
        return -1;
    if (count > 1)
        (void)sd_cmd(12, 0, CMD_RSP48B | CMD_CRC | CMD_IXCHK, resp);
    return 0;
}

static int sd_find_peakfs(void) {
    uint8_t mbr[512];
    part_lba0 = 0;
    part_count = 0;
    if (sd_xfer(0, 1, mbr, 0) != 0)
        return -1;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA)
        return -1;
    for (int i = 0; i < 4; i++) {
        uint8_t *e = mbr + 446 + i * 16;
        uint8_t typ = e[4];
        uint32_t start = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                         ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t count = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                         ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        if (typ == 0x83 && count > 0) {
            part_lba0 = start;
            part_count = count;
            char msg[48];
            snprintf(msg, sizeof(msg), "sdhci: PeakFS partition @ LBA %u\n",
                     (unsigned)part_lba0);
            serial_log(SERIAL_LOG_DEBUG, msg);
            return 0;
        }
    }
    /* Whole-card fallback */
    part_lba0 = 0;
    part_count = 0xffffffffull;
    return 0;
}

static int sdhci_present(void) {
    return sdhci_ok && card_ready;
}

static int sdhci_read(uint64_t lba, uint32_t count, void *buf) {
    if (!sdhci_present())
        return -1;
    if (part_count && lba + count > part_count)
        return -1;
    /* PIO path — no DMA cache maintenance required. */
    return sd_xfer(part_lba0 + lba, count, buf, 0);
}

static int sdhci_write(uint64_t lba, uint32_t count, const void *buf) {
    if (!sdhci_present())
        return -1;
    if (part_count && lba + count > part_count)
        return -1;
    return sd_xfer(part_lba0 + lba, count, (void *)buf, 1);
}

static int sdhci_flush(void) {
    if (!sdhci_present())
        return -1;
    /* Finish any outstanding CMD/DAT activity, then CMD13 until READY_FOR_DATA
     * in Tran state. SD has no ATA-style FLUSH CACHE; this is the durable barrier. */
    sd_wait_idle();
    uint32_t resp[4];
    for (int i = 0; i < 1000; i++) {
        if (sd_cmd(13, rca, CMD_RSP48 | CMD_CRC | CMD_IXCHK, resp) != 0)
            return -1;
        uint32_t st = resp[0];
        int ready = (st & (1u << 8)) != 0;           /* READY_FOR_DATA */
        int state = (int)((st >> 9) & 0xfu);         /* CURRENT_STATE */
        if (ready && state == 4)                     /* Tran */
            return 0;
        sd_delay();
    }
    return -1;
}

static const struct blockdev_ops sdhci_ops = {
    .name = "sdhci",
    .present = sdhci_present,
    .read = sdhci_read,
    .write = sdhci_write,
    .flush = sdhci_flush,
};

void rpi_sdhci_init(void) {
    sd = (volatile uint32_t *)(uintptr_t)rpi_get()->sdhci_base;
    sdhci_ok = 0;
    card_ready = 0;
    if (!sd)
        return;
    if (sd_card_init() != 0) {
        serial_log(SERIAL_LOG_WARN, "rpi: sdhci card init failed\n");
        return;
    }
    if (sd_find_peakfs() != 0) {
        serial_log(SERIAL_LOG_WARN, "rpi: sdhci MBR scan failed\n");
        return;
    }
    sdhci_ok = 1;
    serial_log(SERIAL_LOG_INFO, "rpi: sdhci ready\n");
}

void blockdev_register_sdhci(void) {
    if (sdhci_ok && card_ready)
        blockdev_register(&sdhci_ops);
}
