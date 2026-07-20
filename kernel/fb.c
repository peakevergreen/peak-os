#include "fb.h"
#include "util.h"

extern const uint8_t font8x16[256][16];

static struct framebuffer g_fb;

void fb_init(struct framebuffer *fb) {
    g_fb = *fb;
}

struct framebuffer *fb_get(void) {
    return &g_fb;
}

uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_fb.width || y >= g_fb.height)
        return;
    uint32_t *pixel = (uint32_t *)(g_fb.addr + y * g_fb.pitch + x * 4);
    *pixel = color;
}

uint32_t fb_get_pixel(uint32_t x, uint32_t y) {
    if (x >= g_fb.width || y >= g_fb.height)
        return 0;
    uint32_t *pixel = (uint32_t *)(g_fb.addr + y * g_fb.pitch + x * 4);
    return *pixel;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++)
            fb_put_pixel(x + col, y + row, color);
    }
}

void fb_clear(uint32_t color) {
    fb_fill_rect(0, 0, (uint32_t)g_fb.width, (uint32_t)g_fb.height, color);
}

void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = font8x16[(uint8_t)c];
    for (uint32_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += 16;
        } else {
            fb_draw_char(cx, y, *s, fg, bg);
            cx += 8;
        }
        s++;
    }
}
