#ifndef PEAK_BOOT_SHA256_H
#define PEAK_BOOT_SHA256_H

#include <stdint.h>
#include "boot_util.h"

#define BOOT_SHA256_DIGEST_LEN 32

struct boot_sha256_ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  count; /* bytes in buf[0..count) */
    uint8_t  buf[64];
};

void boot_sha256_init(struct boot_sha256_ctx *ctx);
void boot_sha256_update(struct boot_sha256_ctx *ctx, const void *data, size_t len);
void boot_sha256_final(struct boot_sha256_ctx *ctx, uint8_t out[BOOT_SHA256_DIGEST_LEN]);

#endif
