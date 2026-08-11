#include "gui.h"
#include "fb.h"
#include "theme.h"

void window_draw_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       const char *title, uint32_t bg) {
    const struct peak_theme *t = theme_get();
    uint32_t border = t->border;
    uint32_t title_bg = t->title;
    uint32_t title_fg = t->fg;
    uint32_t shadow = t->bg;
    uint32_t s = fb_ui_scale();
    uint32_t title_h = desktop_title_h();
    uint32_t sh = 4 * s;

    fb_fill_rect(x + sh, y + sh, w, h, shadow);
    fb_fill_rect(x, y, w, h, bg);
    fb_fill_rect(x, y, w, s, border);
    fb_fill_rect(x, y + h - s, w, s, border);
    fb_fill_rect(x, y, s, h, border);
    fb_fill_rect(x + w - s, y, s, h, border);
    fb_fill_rect(x + s, y + s, w - 2 * s, title_h, title_bg);
    /* Leave room past outer border so glyphs are not flush/clipped. */
    fb_draw_string(x + 8 * s, y + 3 * s, title, title_fg, title_bg);
}
