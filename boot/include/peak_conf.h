#ifndef PEAK_CONF_H
#define PEAK_CONF_H

#include "peak_boot.h"
#include "boot_util.h"

struct peak_loader_conf {
    uint16_t width;
    uint16_t height;
    uint16_t bpp;
    struct peak_net_config net;
};

/* Fill defaults (1920x1080, dhcp_fallback + QEMU user-net static). */
void peak_conf_defaults(struct peak_loader_conf *out);

/* Parse key=value text from peak.conf. Unknown keys ignored. */
void peak_conf_parse(const char *text, size_t len, struct peak_loader_conf *out);

#endif
