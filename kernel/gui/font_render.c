#include "font_render.h"

extern const uint8_t font8x16[256][16];

#define FONT_INK_SPANS 64

static char ink_c;
static uint32_t ink_fg, ink_scale;
static struct {
    uint8_t row;
    uint8_t start;
    uint8_t run;
} ink_spans[FONT_INK_SPANS];
static int ink_span_n;

static uint32_t bg_x, bg_y, bg_color, bg_cw, bg_ch;
static int bg_valid;

void font_render_invalidate(void) {
    ink_span_n = -1;
    bg_valid = 0;
}

static void rebuild_ink_cache(char c, uint32_t fg, uint32_t scale) {
    (void)fg;
    ink_c = c;
    ink_scale = scale;
    ink_span_n = 0;
    const uint8_t *glyph = font8x16[(uint8_t)c];
    for (uint8_t row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        uint8_t col = 0;
        while (col < 8 && ink_span_n < FONT_INK_SPANS) {
            while (col < 8 && !(bits & (0x80 >> col)))
                col++;
            if (col >= 8)
                break;
            uint8_t start = col;
            while (col < 8 && (bits & (0x80 >> col)))
                col++;
            ink_spans[ink_span_n].row = row;
            ink_spans[ink_span_n].start = start;
            ink_spans[ink_span_n].run = (uint8_t)(col - start);
            ink_span_n++;
        }
    }
}

void font_render_cell_bg(uint32_t x, uint32_t y, uint32_t cw, uint32_t ch,
                         uint32_t bg, font_fill_fn fill) {
    if (bg_valid && bg_x == x && bg_y == y && bg_color == bg &&
        bg_cw == cw && bg_ch == ch)
        return;
    fill(x, y, cw, ch, bg);
    bg_x = x;
    bg_y = y;
    bg_color = bg;
    bg_cw = cw;
    bg_ch = ch;
    bg_valid = 1;
}

void font_render_glyph(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t scale,
                       uint32_t cw, uint32_t ch, font_fill_fn fill) {
    (void)ch;
    if (ink_span_n < 0 || c != ink_c || fg != ink_fg || scale != ink_scale)
        rebuild_ink_cache(c, fg, scale);
    for (int i = 0; i < ink_span_n; i++) {
        uint8_t row = ink_spans[i].row;
        uint32_t px = x + (uint32_t)ink_spans[i].start * scale;
        uint32_t py = y + (uint32_t)row * scale;
        uint32_t rw = (uint32_t)ink_spans[i].run * scale;
        uint32_t rh = scale;
        if (scale <= 2) {
            uint32_t gh = 16 * scale;
            uint32_t hx = px + 1;
            uint32_t hy = py + 1;
            if (hx + rw <= x + cw && hy + rh <= y + gh)
                fill(hx, hy, rw, rh, 0x00000000);
        }
        fill(px, py, rw, rh, fg);
    }
}
