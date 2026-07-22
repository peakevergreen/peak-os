#ifndef PEAK_BLOCKDEV_H
#define PEAK_BLOCKDEV_H

#include "types.h"

#define BLOCKDEV_SECTOR_SIZE 512

struct blockdev_ops {
    const char *name;
    int (*present)(void);
    int (*read)(uint64_t lba, uint32_t count, void *buf);
    int (*write)(uint64_t lba, uint32_t count, const void *buf);
    int (*flush)(void);
};

void blockdev_register(const struct blockdev_ops *ops);
const struct blockdev_ops *blockdev_get(void);
int  blockdev_present(void);
int  blockdev_read(uint64_t lba, uint32_t count, void *buf);
int  blockdev_write(uint64_t lba, uint32_t count, const void *buf);
int  blockdev_flush(void);

/* Built-in backends */
void blockdev_register_ata(void);
void blockdev_register_sdhci(void);

#endif
