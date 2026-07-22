#ifndef PEAK_FONT_RENDER_H
#define PEAK_FONT_RENDER_H

#include "types.h"

typedef void (*font_fill_fn)(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t color);

void font_render_invalidate(void);
void font_render_cell_bg(uint32_t x, uint32_t y, uint32_t cw, uint32_t ch,
                         uint32_t bg, font_fill_fn fill);
void font_render_glyph(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t scale,
                       uint32_t cw, uint32_t ch, font_fill_fn fill);

#endif
