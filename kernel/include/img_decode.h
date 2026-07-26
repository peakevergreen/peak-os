#ifndef PEAK_IMG_DECODE_H
#define PEAK_IMG_DECODE_H

#include "types.h"

struct img_decoded {
    uint32_t w;
    uint32_t h;
    uint8_t *rgb;
    size_t rgb_len;
};

void img_decode_free(struct img_decoded *img);
int img_decode_ppm(const uint8_t *data, size_t len, struct img_decoded *out);
int img_decode_bmp(const uint8_t *data, size_t len, struct img_decoded *out);
int img_decode_png(const uint8_t *data, size_t len, struct img_decoded *out);
int img_decode_jpeg(const uint8_t *data, size_t len, struct img_decoded *out);
int img_decode_mem(const uint8_t *data, size_t len, struct img_decoded *out);
int img_decode_file(const char *path, struct img_decoded *out);

#endif
