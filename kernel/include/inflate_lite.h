#ifndef PEAK_INFLATE_LITE_H
#define PEAK_INFLATE_LITE_H

#include "types.h"

/* Raw DEFLATE (RFC 1951). Returns 0 on success. */
int inflate_lite(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                 size_t *out_len);

/* Zlib wrapper (RFC 1950): 2-byte header + DEFLATE + optional Adler32. */
int inflate_lite_zlib(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap,
                      size_t *out_len);

#endif
