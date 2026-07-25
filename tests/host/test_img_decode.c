#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "img_decode.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void *host_kmalloc(size_t n) {
    return malloc(n);
}

static void host_kfree(void *p) {
    free(p);
}

/* Stubs for kernel heap used by img_decode.c */
void *kmalloc(size_t n) { return host_kmalloc(n); }
void kfree(void *p) { host_kfree(p); }

static const char ppm_ok[] =
    "P6\n3 2\n255\n"
    "\x01\x02\x03"
    "\x04\x05\x06"
    "\x07\x08\t"
    "\x0a\x0b\x0c"
    "\x0d\x0e\x0f"
    "\x10\x11\x12";

static const uint8_t bmp_ok[] = {
    0x42,0x4d,0x46,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x36,0x00,0x00,0x00,
    0x28,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x00,
    0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0x00,0x00,0x00,0x00,0xff,0x00,0x00,
    0x00,0x00,0xff,0x00,0xff,0xff,0xff,0x00
};

int main(void) {
    struct img_decoded img;
    memset(&img, 0, sizeof(img));
    expect(img_decode_ppm((const uint8_t *)ppm_ok, sizeof(ppm_ok) - 1, &img) == 0, "ppm parse");
    expect(img.w == 3 && img.h == 2, "ppm dims");
    expect(img.rgb && img.rgb[0] == 1 && img.rgb[5] == 6, "ppm pixels");
    img_decode_free(&img);

    memset(&img, 0, sizeof(img));
    expect(img_decode_bmp(bmp_ok, sizeof(bmp_ok), &img) == 0, "bmp parse");
    expect(img.w == 2 && img.h == 2, "bmp dims");
    expect(img.rgb && img.rgb[0] == 255 && img.rgb[1] == 0 && img.rgb[2] == 0, "bmp bgr->rgb");
    img_decode_free(&img);

    expect(img_decode_ppm((const uint8_t *)"P5\n1 1\n255\n0", 14, &img) != 0, "reject P5");
    expect(img_decode_bmp(bmp_ok, 10, &img) != 0, "reject short bmp");

    if (fails) {
        fprintf(stderr, "%d img_decode test(s) failed\n", fails);
        return 1;
    }
    printf("test_img_decode: ok\n");
    return 0;
}
