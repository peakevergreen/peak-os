#ifndef PEAK_FB_H
#define PEAK_FB_H

#include "types.h"

struct framebuffer {
    uint8_t  *addr;
    uint64_t  width;
    uint64_t  height;
    uint64_t  pitch;
    uint16_t  bpp;
    uint8_t   red_mask_size;
    uint8_t   red_mask_shift;
    uint8_t   green_mask_size;
    uint8_t   green_mask_shift;
    uint8_t   blue_mask_size;
    uint8_t   blue_mask_shift;
};

void fb_init(struct framebuffer *fb);
struct framebuffer *fb_get(void);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(uint32_t x, uint32_t y);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(uint32_t color);
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

#endif
