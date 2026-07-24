#ifndef PEAK_PEAKDISK_H
#define PEAK_PEAKDISK_H

#include "types.h"

/* Persist VFS (PEAKFS1) to ATA disk starting at LBA 1.
 * Encrypted envelopes: PEAKDSK3 = passphrase PBKDF2 → volume key (no key in header).
 * PEAKDSK2 header-key volumes are rejected on load (retired). */
void peakdisk_init(void);
int  peakdisk_available(void);
int  peakdisk_save(void);
/* Queue save on a worker kthread (non-blocking). */
int  peakdisk_save_async(void);
int  peakdisk_load(void);
int  peakdisk_busy(void);
const char *peakdisk_last_error(void);
uint32_t peakdisk_last_save_bytes(void);
/* Set unlock passphrase for PEAKDSK3 save/load (cleared with NULL/empty). */
void peakdisk_set_passphrase(const char *pass);

#endif
