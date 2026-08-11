/*
 * Host test: software key-repeat must clear on any key-up (Shift remap case).
 */
#include "keyboard.h"

#include <stdio.h>
#include <stdint.h>

static int fails;
static uint64_t fake_ticks;

uint64_t timer_ticks(void) {
    return fake_ticks;
}

static void expect(int c, const char *m) {
    if (!c) {
        fprintf(stderr, "FAIL: %s\n", m);
        fails++;
    }
}

static int drain_count(void) {
    int n = 0;
    while (keyboard_try_getkey() != 0)
        n++;
    return n;
}

int main(void) {
    keyboard_init();
    keyboard_set_repeat(1, 1);

    keyboard_set_modifiers(1, 0, 0);
    keyboard_inject('A');
    keyboard_repeat_key_down('A');
    drain_count();
    keyboard_set_modifiers(0, 0, 0);
    keyboard_repeat_key_up('a'); /* mismatched peak — must still clear */

    fake_ticks = 100;
    for (int i = 0; i < 20; i++) {
        fake_ticks++;
        keyboard_poll();
    }
    expect(drain_count() == 0, "no sticky repeat after mismatched key-up");

    keyboard_repeat_key_down('x');
    keyboard_repeat_key_up('x');
    fake_ticks += 50;
    for (int i = 0; i < 20; i++) {
        fake_ticks++;
        keyboard_poll();
    }
    expect(drain_count() == 0, "cleared on matching key-up");

    keyboard_repeat_clear();
    if (fails) {
        fprintf(stderr, "%d keyboard repeat test(s) failed\n", fails);
        return 1;
    }
    printf("test_keyboard_repeat: ok\n");
    return 0;
}
