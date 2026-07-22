#ifndef PEAK_ATA_H
#define PEAK_ATA_H

#include "types.h"

#define ATA_SECTOR_SIZE 512

void ata_init(void);
int  ata_present(void);
/* LBA28 read/write (chunked). Returns 0 on success. */
int  ata_read_sectors(uint32_t lba, uint32_t count, void *buf);
int  ata_write_sectors(uint32_t lba, uint32_t count, const void *buf);
int  ata_flush(void);

#endif
