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
/* After PMM/VMM: allocate a back buffer sized to the real framebuffer. */
void fb_alloc_backbuffer(void);
struct framebuffer *fb_get(void);

void fb_set_ui_scale(uint32_t scale); /* 1..4 */
uint32_t fb_ui_scale(void);
/* Suggested scale for current framebuffer (larger on 1080p+). */
uint32_t fb_recommend_scale(void);
uint32_t fb_char_w(void);  /* glyph width  = 8 * scale */
uint32_t fb_char_h(void);  /* glyph height = 16 * scale */
uint32_t fb_cell_w(void);  /* advance width  (= char_w) */
uint32_t fb_cell_h(void);  /* advance height = char_h + gap */

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t fb_get_pixel(uint32_t x, uint32_t y);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(uint32_t color);
/* Draw glyph in a full cell: fill bg for cell, then fg ink (no clipping into next row) */
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);
/* Like fb_draw_string but stops before exceeding max_w pixels. */
void fb_draw_string_fit(uint32_t x, uint32_t y, uint32_t max_w, const char *s,
                        uint32_t fg, uint32_t bg);
uint32_t fb_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Software double-buffer for tear-free GUI compositing */
int  fb_backbuffer_ok(void);
uint32_t *fb_back_buf(void); /* tightly packed width*height; NULL if unavailable */
void fb_begin_frame(void); /* draw into back buffer (no-op if unavailable) */
void fb_cancel_frame(void); /* stop targeting back without presenting */
void fb_end_frame(void);   /* blit back → front in one pass */
/* Present only a rectangle (back → front). Back must already be updated. */
void fb_present_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* Copy pre-scaled ARGB into the current draw target (back or front). */
void fb_blit_argb(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  const uint32_t *src, uint32_t src_stride);
/* Copy a rectangle from the back buffer into a tightly packed dest. */
void fb_copy_from_back(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t *dst, uint32_t dst_stride);
/* Copy tightly packed src into the back buffer (no present). */
void fb_copy_to_back(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     const uint32_t *src, uint32_t src_stride);

/*
 * Redirect drawing (fill/blit/text) to an offscreen ARGB buffer.
 * stride is pixels per row. Pass px=NULL to restore the normal target.
 */
void fb_set_draw_target(uint32_t *px, uint32_t w, uint32_t h, uint32_t stride);

/* Read/write the *visible* front buffer (for mouse cursor overlay) */
uint32_t fb_front_get_pixel(uint32_t x, uint32_t y);
void fb_front_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_front_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
/* Copy rect from last composed back buffer → front (cursor erase) */
void fb_restore_from_back(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

#endif
