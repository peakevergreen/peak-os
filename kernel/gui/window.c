#include "gui.h"
#include "fb.h"

void window_draw_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const char *title, uint32_t bg) {
    uint32_t border = fb_rgb(0x2A, 0x4A, 0x3A);
    uint32_t title_bg = fb_rgb(0x1A, 0x3D, 0x2C);
    uint32_t title_fg = fb_rgb(0xE8, 0xF0, 0xEA);
    uint32_t shadow = fb_rgb(0x05, 0x0A, 0x08);

    /* drop shadow */
    fb_fill_rect(x + 4, y + 4, w, h, shadow);
    /* body */
    fb_fill_rect(x, y, w, h, bg);
    /* border */
    fb_fill_rect(x, y, w, 1, border);
    fb_fill_rect(x, y + h - 1, w, 1, border);
    fb_fill_rect(x, y, 1, h, border);
    fb_fill_rect(x + w - 1, y, 1, h, border);
    /* title bar */
    fb_fill_rect(x + 1, y + 1, w - 2, 22, title_bg);
    fb_draw_string(x + 10, y + 4, title, title_fg, title_bg);
}
