/*
 * Host tests for userspace GUI protocol dispatch (links kernel/gui/guiproto.c).
 * Also covers soft surface-budget reclaim (test build uses 512 KiB budget).
 */
#include "types.h"
#include "guiproto.h"
#include "surface.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails++;
    }
}

static void test_surface_budget_reclaim(void) {
    struct win_surface a, b, c;

    surface_init();
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));

    expect(surface_budget() == SURFACE_BUDGET_BYTES, "budget constant");
    expect(surface_bytes_used() == 0, "bytes used starts empty");
    expect(surface_ensure(NULL, 16, 16) == SURFACE_ERR_INVAL, "null surface");
    expect(surface_ensure(&a, 0, 16) == SURFACE_ERR_INVAL, "zero width");

    /* 256x256 ARGB = 256 KiB each; two fit in 512 KiB soft budget. */
    expect(surface_ensure(&a, 256, 256) == SURFACE_OK, "alloc a");
    expect(surface_ensure(&b, 256, 256) == SURFACE_OK, "alloc b");
    expect(surface_bytes_used() == 2ull * 256ull * 256ull * 4ull, "two surfaces tracked");
    expect(surface_pressure_pct() == 100, "at soft budget");

    /* Third active surface exceeds budget with nothing reclaimable → refuse. */
    expect(surface_ensure(&c, 256, 256) == SURFACE_ERR_BUDGET, "budget refuse");
    expect(c.px == NULL, "c not allocated");
    expect(surface_bytes_used() == 2ull * 256ull * 256ull * 4ull, "usage unchanged");

    /* Mark b unused; next ensure reclaims b and admits c. */
    surface_set_reclaimable(&b, 1);
    expect(surface_ensure(&c, 256, 256) == SURFACE_OK, "alloc c after reclaim");
    expect(c.px != NULL, "c has pixels");
    expect(b.px == NULL, "b reclaimed");
    expect(b.reclaimable == 0, "reclaim clears flag");
    expect(surface_bytes_used() == 2ull * 256ull * 256ull * 4ull, "still two live");
    expect(c.reclaimable == 0, "active surface not reclaimable");

    /* Explicit reclaim of remaining reclaimable (none) then free. */
    surface_set_reclaimable(&a, 1);
    expect(surface_reclaim(0, &c) >= 256ull * 256ull * 4ull, "reclaim all unused");
    expect(a.px == NULL, "a reclaimed by API");
    expect(surface_bytes_used() == 256ull * 256ull * 4ull, "only c remains");

    surface_free(&c);
    surface_free(&b);
    surface_free(&a);
    expect(surface_bytes_used() == 0, "freed to zero");
}

int main(void) {
    surface_init();
    guiproto_init();
    expect(guiproto_window_count() == 0, "empty after init");

    struct gui_msg msg;
    memset(&msg, 0, sizeof(msg));
    msg.op = GUI_OP_CREATE;
    msg.x = 10;
    msg.y = 20;
    msg.w = 120;
    msg.h = 80;
    snprintf(msg.title, sizeof(msg.title), "Host");
    int win = guiproto_dispatch(&msg);
    expect(win == 0, "create returns slot 0");
    expect(guiproto_window_count() == 1, "one window");

    msg.op = GUI_OP_ATTACH;
    msg.win_id = (uint32_t)win;
    msg.w = 64;
    msg.h = 48;
    expect(guiproto_dispatch(&msg) == 0, "attach backing store");
    expect(guiproto_surface((uint32_t)win) != NULL, "surface allocated");
    expect(guiproto_surface((uint32_t)win)->w == 64, "surface width");

    msg.op = GUI_OP_DAMAGE;
    msg.win_id = (uint32_t)win;
    msg.x = 4;
    msg.y = 6;
    msg.w = 12;
    msg.h = 8;
    expect(guiproto_dispatch(&msg) == 0, "partial damage");
    uint32_t dx, dy, dw, dh;
    expect(guiproto_take_damage((uint32_t)win, &dx, &dy, &dw, &dh) == 1,
           "damage pending");
    expect(dx == 4 && dy == 6 && dw == 12 && dh == 8, "damage rect");
    expect(guiproto_take_damage((uint32_t)win, &dx, &dy, &dw, &dh) == 0,
           "damage consumed");

    msg.op = GUI_OP_SET_TITLE;
    snprintf(msg.title, sizeof(msg.title), "Renamed");
    expect(guiproto_dispatch(&msg) == 0, "set title");

    msg.op = GUI_OP_MOVE;
    msg.x = 30;
    msg.y = 40;
    expect(guiproto_dispatch(&msg) == 0, "move window");
    expect(guiproto_take_damage((uint32_t)win, &dx, &dy, &dw, &dh) == 1,
           "move marks damage");

    msg.op = GUI_OP_DESTROY;
    expect(guiproto_dispatch(&msg) == 0, "destroy");
    expect(guiproto_window_count() == 0, "window gone");
    expect(guiproto_surface((uint32_t)win) == NULL, "surface freed");

    guiproto_init();
    for (int i = 0; i < 16; i++) {
        msg.op = GUI_OP_CREATE;
        msg.title[0] = (char)('A' + i);
        msg.title[1] = '\0';
        expect(guiproto_dispatch(&msg) == i, "fill slots");
    }
    msg.op = GUI_OP_CREATE;
    expect(guiproto_dispatch(&msg) == -1, "table full");
    expect(guiproto_window_count() == 16, "max windows");

    test_surface_budget_reclaim();

    if (fails) {
        fprintf(stderr, "%d guiproto test(s) failed\n", fails);
        return 1;
    }
    printf("test_guiproto: ok\n");
    return 0;
}
