/*
 * Host tests for display present rect clipping (shared with display.c).
 */
#include <stdio.h>
#include <stdint.h>

#include "../../kernel/include/display_clip.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    uint32_t x, y, w, h;

    expect(display_clip_rect(640, 480, 10, 20, 100, 50, &x, &y, &w, &h) == 1,
           "interior rect kept");
    expect(x == 10 && y == 20 && w == 100 && h == 50, "interior coords");

    expect(display_clip_rect(640, 480, 600, 0, 100, 10, &x, &y, &w, &h) == 1,
           "right edge clip");
    expect(x == 600 && w == 40, "right edge width");

    expect(display_clip_rect(640, 480, 0, 470, 10, 20, &x, &y, &w, &h) == 1,
           "bottom edge clip");
    expect(y == 470 && h == 10, "bottom edge height");

    expect(display_clip_rect(640, 480, 630, 470, 20, 20, &x, &y, &w, &h) == 1,
           "corner clip both edges");
    expect(x == 630 && y == 470 && w == 10 && h == 10, "corner clipped size");

    expect(display_clip_rect(640, 480, 0, 0, 640, 480, &x, &y, &w, &h) == 1,
           "full framebuffer kept");
    expect(w == 640 && h == 480, "full framebuffer size");

    expect(display_clip_rect(640, 480, 639, 479, 1, 1, &x, &y, &w, &h) == 1,
           "exact last pixel kept");
    expect(x == 639 && y == 479 && w == 1 && h == 1, "exact last pixel coords");

    expect(display_clip_rect(640, 480, 640, 0, 1, 1, &x, &y, &w, &h) == 0,
           "x == width rejected");
    expect(display_clip_rect(640, 480, 0, 480, 1, 1, &x, &y, &w, &h) == 0,
           "y == height rejected");

    expect(display_clip_rect(640, 480, 700, 0, 10, 10, &x, &y, &w, &h) == 0,
           "fully off-screen x");
    expect(display_clip_rect(640, 480, 0, 500, 10, 10, &x, &y, &w, &h) == 0,
           "fully off-screen y");
    expect(display_clip_rect(640, 480, 0, 0, 0, 10, &x, &y, &w, &h) == 0,
           "zero width");
    expect(display_clip_rect(640, 480, 0, 0, 10, 0, &x, &y, &w, &h) == 0,
           "zero height");
    expect(display_clip_rect(0, 480, 0, 0, 10, 10, &x, &y, &w, &h) == 0,
           "zero fb width");
    expect(display_clip_rect(640, 0, 0, 0, 10, 10, &x, &y, &w, &h) == 0,
           "zero fb height");

    /* uint32 wrap: naive x+w would underflow and skip the clip. */
    expect(display_clip_rect(640, 480, 600, 0, 0xFFFFFFF0u, 10, &x, &y, &w, &h) == 1,
           "overflowing width clips");
    expect(x == 600 && w == 40, "overflowing width result");
    expect(display_clip_rect(640, 480, 0, 400, 10, 0xFFFFFFF0u, &x, &y, &w, &h) == 1,
           "overflowing height clips");
    expect(y == 400 && h == 80, "overflowing height result");
    expect(display_clip_rect(640, 480, 0xFFFFFFF0u, 0, 0x20, 10, &x, &y, &w, &h) == 0,
           "overflowing x fully off-screen");

    expect(display_clip_rect(640, 480, 10, 20, 30, 40, NULL, NULL, NULL, NULL) == 1,
           "NULL out pointers allowed");

    if (fails) {
        fprintf(stderr, "%d display_present test(s) failed\n", fails);
        return 1;
    }
    printf("test_display_present: ok\n");
    return 0;
}
