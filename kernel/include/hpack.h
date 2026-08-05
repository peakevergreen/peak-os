#ifndef PEAK_HPACK_H
#define PEAK_HPACK_H

#include "types.h"

int hpack_huff_decode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);
int hpack_enc_int(uint8_t *buf, size_t cap, size_t *o, uint32_t v, unsigned nbits,
                  uint8_t prefix_hi);
int hpack_add_lit(uint8_t *buf, size_t cap, size_t *o, uint32_t name_idx, const char *val);
int hpack_decode_block(const uint8_t *p, size_t len, int *status_out, char *hdr_buf,
                       size_t hdr_cap, size_t *hdr_off);

#endif
