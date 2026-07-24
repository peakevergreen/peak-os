#ifndef PEAK_BOOT_VERIFY_H
#define PEAK_BOOT_VERIFY_H

#include <stdint.h>
#include "boot_util.h"
#include "peak_conf.h"

/*
 * Software kernel digest check before boot_elf_load.
 * Expected digest may come from peak.conf (kernel_sha256=) and/or a SHA256SUMS
 * stub on the boot volume. Fail closed when conf.verify_required is set.
 */
int boot_verify_kernel(const struct peak_loader_conf *conf,
                       const uint8_t *img, size_t size,
                       const char *manifest, size_t manifest_len);

#endif
