#ifndef PEAK_DISPLAY_CLIP_H
#define PEAK_DISPLAY_CLIP_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#else
#include "types.h"
#endif

/*
 * Clip a present rect to the visible framebuffer.
 * Returns 1 when any pixels remain; 0 when fully outside or zero-sized.
 */
int display_clip_rect(uint32_t fb_w, uint32_t fb_h,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t *ox, uint32_t *oy, uint32_t *ow, uint32_t *oh);

#endif
