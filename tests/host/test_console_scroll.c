/*
 * Host tests for CLI console scroll geometry (front-FB policy helper).
 */
#include <stdio.h>
#include <stdint.h>

#include "../../kernel/include/console_scroll.h"

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

int main(void) {
    uint32_t rows = 0;

    expect(console_scroll_plan(0, 16, &rows) == 0, "height 0 no-op");
    expect(console_scroll_plan(16, 16, &rows) == 0, "height==glyph no-op");
    expect(console_scroll_plan(100, 0, &rows) == 0, "glyph 0 no-op");
    expect(console_scroll_plan(15, 16, &rows) == 0, "glyph taller than fb no-op");
    expect(console_scroll_plan(48, 16, &rows) == 1, "normal scroll");
    expect(rows == 32, "copy_rows = height - glyph");
    expect(console_scroll_plan(800, 48, &rows) == 1 && rows == 752, "scale-3 cell");

    expect(console_scroll_plan(49, 17, NULL) == 1, "NULL copy_rows allowed");
    expect(console_scroll_plan(17, 17, &rows) == 0, "exact fit no scroll");
    expect(console_scroll_plan(18, 17, &rows) == 1 && rows == 1, "one-row scroll");

    expect(console_scroll_bytes(4096, 752) == (uint64_t)4096 * 752,
           "scroll byte count");
    expect(console_scroll_bytes(0, 100) == 0, "zero pitch bytes");
    expect(console_scroll_bytes(4096, 0) == 0, "zero rows bytes");
    /* Front-buffer memmove size must stay in 64-bit (pitch * rows). */
    expect(console_scroll_bytes(0x10000u, 0x10000u) == (1ull << 32),
           "large scroll bytes stay 64-bit");

    if (fails) {
        fprintf(stderr, "%d console_scroll test(s) failed\n", fails);
        return 1;
    }
    printf("test_console_scroll: ok\n");
    return 0;
}
