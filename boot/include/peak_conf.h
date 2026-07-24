#ifndef PEAK_CONF_H
#define PEAK_CONF_H

#include "peak_boot.h"
#include "boot_util.h"
#include "boot_sha256.h"

struct peak_loader_conf {
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    uint8_t smoke_persist; /* 1 = PeakFS smoke save/restore after boot */
    uint8_t verify_required; /* 1 = fail closed without matching kernel digest */
    char kernel_sha256[BOOT_SHA256_DIGEST_LEN * 2 + 1]; /* optional override */
    struct peak_net_config net;
};

/* Fill defaults (1920x1080, dhcp_fallback + QEMU user-net static). */
void peak_conf_defaults(struct peak_loader_conf *out);

/* Parse key=value text from peak.conf. Unknown keys ignored. */
void peak_conf_parse(const char *text, size_t len, struct peak_loader_conf *out);

#endif
