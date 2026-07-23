#ifdef PEAK_HOST_TEST
#include "include/display_clip.h"
#else
#include "display_clip.h"
#endif

int display_clip_rect(uint32_t fb_w, uint32_t fb_h,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh) {
    if (!w || !h || !fb_w || !fb_h)
        return 0;
    if (x >= fb_w || y >= fb_h)
        return 0;
    /* Avoid uint32 x+w / y+h wrap — use remaining extent instead. */
    if (w > fb_w - x)
        w = fb_w - x;
    if (h > fb_h - y)
        h = fb_h - y;
    if (!w || !h)
        return 0;
    if (ox)
        *ox = x;
    if (oy)
        *oy = y;
    if (ow)
        *ow = w;
    if (oh)
        *oh = h;
    return 1;
}
