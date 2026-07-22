#include "ata.h"
#include "util.h"

#define ATA_DATA       0x1F0
#define ATA_SECCOUNT   0x1F2
#define ATA_LBA_LO     0x1F3
#define ATA_LBA_MID    0x1F4
#define ATA_LBA_HI     0x1F5
#define ATA_DRIVE      0x1F6
#define ATA_STATUS     0x1F7
#define ATA_CMD        0x1F7
#define ATA_ALT_STATUS 0x3F6

#define ATA_SR_BSY  0x80
#define ATA_SR_DRQ  0x08
#define ATA_SR_ERR  0x01

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENT  0xEC
#define ATA_CMD_FLUSH  0xE7

#define ATA_MAX_XFER   256

static int have_disk;

static void ata_delay(void) {
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
    inb(ATA_ALT_STATUS);
}

static int ata_wait_bsy(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(ATA_STATUS) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & ATA_SR_ERR)
            return -1;
        if (st & ATA_SR_DRQ)
            return 0;
    }
    return -1;
}

void ata_init(void) {
    have_disk = 0;
    outb(ATA_DRIVE, 0xA0);
    ata_delay();
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, ATA_CMD_IDENT);
    ata_delay();
    uint8_t st = inb(ATA_STATUS);
    if (st == 0)
        return;
    if (ata_wait_bsy() != 0)
        return;
    if (ata_wait_drq() != 0)
        return;
    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);
    (void)id;
    have_disk = 1;
}

int ata_present(void) {
    return have_disk;
}

/* Single PIO command: at most ATA_MAX_XFER sectors. */
static int ata_pio_xfer_chunk(uint32_t lba, uint32_t count, void *buf, int write) {
    if (!have_disk || count == 0 || count > ATA_MAX_XFER)
        return -1;
    if (ata_wait_bsy() != 0)
        return -1;
    outb(ATA_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCOUNT, (uint8_t)(count & 0xFF));
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, write ? ATA_CMD_WRITE : ATA_CMD_READ);
    uint16_t *p = (uint16_t *)buf;
    for (uint32_t s = 0; s < count; s++) {
        if (ata_wait_bsy() != 0 || ata_wait_drq() != 0)
            return -1;
        if (write) {
            for (int i = 0; i < 256; i++)
                outw(ATA_DATA, p[i]);
        } else {
            for (int i = 0; i < 256; i++)
                p[i] = inw(ATA_DATA);
        }
        p += 256;
    }
    return 0;
}

static int ata_pio_xfer(uint32_t lba, uint32_t count, void *buf, int write) {
    if (!have_disk || count == 0)
        return -1;
    uint8_t *p = (uint8_t *)buf;
    while (count) {
        uint32_t n = count > ATA_MAX_XFER ? ATA_MAX_XFER : count;
        if (ata_pio_xfer_chunk(lba, n, p, write) != 0)
            return -1;
        lba += n;
        p += n * ATA_SECTOR_SIZE;
        count -= n;
    }
    return 0;
}

int ata_read_sectors(uint32_t lba, uint32_t count, void *buf) {
    return ata_pio_xfer(lba, count, buf, 0);
}

int ata_write_sectors(uint32_t lba, uint32_t count, const void *buf) {
    return ata_pio_xfer(lba, count, (void *)buf, 1);
}

int ata_flush(void) {
    if (!have_disk)
        return -1;
    if (ata_wait_bsy() != 0)
        return -1;
    outb(ATA_DRIVE, 0xE0);
    outb(ATA_CMD, ATA_CMD_FLUSH);
    return ata_wait_bsy();
}
