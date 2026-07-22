#ifdef PEAK_HOST_TEST
#include "include/console_scroll.h"
#else
#include "console_scroll.h"
#endif

int console_scroll_plan(uint32_t fb_height, uint32_t glyph_h, uint32_t *copy_rows) {
    if (glyph_h == 0 || fb_height <= glyph_h)
        return 0;
    if (copy_rows)
        *copy_rows = fb_height - glyph_h;
    return 1;
}

uint64_t console_scroll_bytes(uint32_t pitch, uint32_t copy_rows) {
    return (uint64_t)pitch * (uint64_t)copy_rows;
}
