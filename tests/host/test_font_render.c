/*
 * Host tests: glyph FG/shadow fills must stay inside the cw×ch cell.
 */
#include "types.h"
#include "font_render.h"

#include <stdio.h>

/* Full-ink glyph so scale*8 / scale*16 can exceed a narrow cell. */
const uint8_t font8x16[256][16] = {
    [0x21] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    },
};

static int fails;
static int spill;
static uint32_t cell_x, cell_y, cell_cw, cell_ch;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void capture_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t color) {
    (void)color;
    if (!w || !h)
        return;
    if (x < cell_x || y < cell_y ||
        x + w > cell_x + cell_cw || y + h > cell_y + cell_ch)
        spill++;
}

static void noop_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
}

int main(void) {
    cell_x = 10;
    cell_y = 20;
    /* scale 3 ink is 24×48; shrink cell so unclipped fills would spill. */
    cell_cw = 20;
    cell_ch = 40;

    font_render_invalidate();
    spill = 0;
    font_render_glyph('!', cell_x, cell_y, 0x00FFFFFF, 3, cell_cw, cell_ch,
                      capture_fill);
    expect(spill == 0, "cached path clips scale-3 ink to cell");

    /* Second draw hits warm cache; still must clip. */
    spill = 0;
    font_render_glyph('!', cell_x, cell_y, 0x00FFFFFF, 3, cell_cw, cell_ch,
                      capture_fill);
    expect(spill == 0, "warm-cache path clips scale-3 ink to cell");

    /* Force cache churn, then re-check clip. */
    for (uint32_t s = 1; s <= 20; s++)
        font_render_glyph('!', 0, 0, 0x1, s, 8 * s, 16 * s + s, noop_fill);
    spill = 0;
    font_render_glyph('!', cell_x, cell_y, 0x00FFFFFF, 3, cell_cw, cell_ch,
                      capture_fill);
    expect(spill == 0, "post-churn path clips scale-3 ink to cell");

    /* scale-1 shadow is +1,+1 and would spill past cw without clipping */
    spill = 0;
    cell_cw = 8;
    cell_ch = 16;
    font_render_invalidate();
    font_render_glyph('!', cell_x, cell_y, 0x00FFFFFF, 1, cell_cw, cell_ch,
                      capture_fill);
    expect(spill == 0, "scale-1 shadow+ink clipped to cell");

    if (fails) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("test_font_render: ok\n");
    return 0;
}
