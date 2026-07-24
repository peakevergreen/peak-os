#ifndef PEAK_WEBPKI_H
#define PEAK_WEBPKI_H

#include "types.h"

struct webpki_root {
    const uint8_t *der;
    size_t der_len;
};

extern const struct webpki_root webpki_roots[];
extern const int webpki_root_count;

/*
 * Build a path from leaf..intermediates in certs[] to an embedded root.
 * Returns 1 if verified, 0 otherwise. Requires RTC for expiry (fail closed
 * if clock unavailable).
 */
int webpki_verify_chain(const uint8_t *const *certs, const size_t *lens, int n,
                        const char *sni_host);

#endif
