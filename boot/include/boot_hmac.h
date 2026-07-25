#ifndef PEAK_BOOT_HMAC_H
#define PEAK_BOOT_HMAC_H

#include <stdint.h>
#include "boot_sha256.h"

void boot_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len,
                      uint8_t out[BOOT_SHA256_DIGEST_LEN]);

#endif
