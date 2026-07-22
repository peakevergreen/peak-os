#ifndef PEAK_PEAKDISK_H
#define PEAK_PEAKDISK_H

#include "types.h"

/* Persist VFS (PEAKFS1) to ATA disk starting at LBA 1. */
void peakdisk_init(void);
int  peakdisk_available(void);
int  peakdisk_save(void);
/* Queue save on a worker kthread (non-blocking). */
int  peakdisk_save_async(void);
int  peakdisk_load(void);
int  peakdisk_busy(void);

#endif
