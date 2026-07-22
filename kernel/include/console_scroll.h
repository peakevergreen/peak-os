#ifndef PEAK_CONSOLE_SCROLL_H
#define PEAK_CONSOLE_SCROLL_H

#ifdef PEAK_HOST_TEST
#include <stdint.h>
#else
#include "types.h"
#endif

/*
 * Pure scroll geometry for the MODE_CLI framebuffer console.
 * Returns 1 if a scroll should copy rows; 0 to no-op.
 * When returning 1, *copy_rows is set to (fb_height - glyph_h).
 *
 * Runtime scroll must target the *front* buffer (fb->addr), never the
 * compositor back buffer — presenting an empty/stale back wipes the boot log.
 */
int console_scroll_plan(uint32_t fb_height, uint32_t glyph_h, uint32_t *copy_rows);

/* Bytes copied when scrolling the CLI front buffer (pitch * copy_rows). */
uint64_t console_scroll_bytes(uint32_t pitch, uint32_t copy_rows);

#endif
