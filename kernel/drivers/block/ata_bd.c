#include "blockdev.h"
#include "ata.h"

static int ata_bd_present(void) {
    return ata_present();
}

static int ata_bd_read(uint64_t lba, uint32_t count, void *buf) {
    if (lba > 0x0FFFFFFFULL)
        return -1;
    return ata_read_sectors((uint32_t)lba, count, buf);
}

static int ata_bd_write(uint64_t lba, uint32_t count, const void *buf) {
    if (lba > 0x0FFFFFFFULL)
        return -1;
    return ata_write_sectors((uint32_t)lba, count, buf);
}

static int ata_bd_flush(void) {
    return ata_flush();
}

static const struct blockdev_ops ata_ops = {
    .name = "ata",
    .present = ata_bd_present,
    .read = ata_bd_read,
    .write = ata_bd_write,
    .flush = ata_bd_flush,
};

void blockdev_register_ata(void) {
    ata_init();
    if (ata_present())
        blockdev_register(&ata_ops);
}
