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

    expect(display_clip_rect(640, 480, 700, 0, 10, 10, &x, &y, &w, &h) == 0,
           "fully off-screen x");
    expect(display_clip_rect(640, 480, 0, 500, 10, 10, &x, &y, &w, &h) == 0,
           "fully off-screen y");
    expect(display_clip_rect(640, 480, 0, 0, 0, 10, &x, &y, &w, &h) == 0,
           "zero width");
    expect(display_clip_rect(640, 480, 0, 0, 10, 0, &x, &y, &w, &h) == 0,
           "zero height");

    if (fails) {
        fprintf(stderr, "%d display_present test(s) failed\n", fails);
        return 1;
    }
    printf("test_display_present: ok\n");
    return 0;
}
