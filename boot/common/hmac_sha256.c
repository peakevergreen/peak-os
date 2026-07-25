#include "boot_hmac.h"
#include "boot_util.h"

void boot_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *msg, size_t msg_len,
                      uint8_t out[BOOT_SHA256_DIGEST_LEN]) {
    uint8_t kpad[64];
    boot_memset(kpad, 0, sizeof(kpad));
    if (key_len > 64) {
        struct boot_sha256_ctx c;
        boot_sha256_init(&c);
        boot_sha256_update(&c, key, key_len);
        boot_sha256_final(&c, kpad);
        key_len = BOOT_SHA256_DIGEST_LEN;
    } else if (key && key_len)
        boot_memcpy(kpad, key, key_len);

    uint8_t ipad[64], opad[64];
    for (size_t i = 0; i < 64; i++) {
        ipad[i] = kpad[i] ^ 0x36;
        opad[i] = kpad[i] ^ 0x5c;
    }

    struct boot_sha256_ctx inner;
    boot_sha256_init(&inner);
    boot_sha256_update(&inner, ipad, 64);
    boot_sha256_update(&inner, msg, msg_len);
    uint8_t inner_hash[BOOT_SHA256_DIGEST_LEN];
    boot_sha256_final(&inner, inner_hash);

    struct boot_sha256_ctx outer;
    boot_sha256_init(&outer);
    boot_sha256_update(&outer, opad, 64);
    boot_sha256_update(&outer, inner_hash, BOOT_SHA256_DIGEST_LEN);
    boot_sha256_final(&outer, out);
}
