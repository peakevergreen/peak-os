#ifndef PEAK_DISPLAY_H
#define PEAK_DISPLAY_H

#include "types.h"
#include "fb.h"

struct display_caps {
    int has_vblank;
    int has_pageflip;
};

/* Bind to the linear framebuffer from BootInfo. Call after fb_init. */
void display_init(struct framebuffer *fb);

struct display_caps display_get_caps(void);

/* Wait for start of vertical blank when the backend supports it (else no-op). */
void display_wait_vblank(void);

/*
 * Present from a tightly packed ARGB/xRGB source (stride = width in pixels)
 * into the visible scanout. Waits for VBlank once when supported unless a
 * frame batch is already open.
 */
void display_present_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          const uint32_t *src, uint32_t src_stride);
void display_present_full(const uint32_t *src);

/*
 * Batch several presents under one VBlank wait:
 *   display_frame_begin();
 *   display_present_rect(...); ...
 *   display_frame_end();
 */
void display_frame_begin(void);
void display_frame_end(void);

#endif
