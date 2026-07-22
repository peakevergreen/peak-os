/*
 * Host tests for userspace GUI protocol dispatch (links kernel/gui/guiproto.c).
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

    if (fails) {
        fprintf(stderr, "%d guiproto test(s) failed\n", fails);
        return 1;
    }
    printf("test_guiproto: ok\n");
    return 0;
}
