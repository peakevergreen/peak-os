#ifndef PEAK_KEYBOARD_H
#define PEAK_KEYBOARD_H

#include "types.h"

/* Special keys (try_getkey); ASCII is 1..127 */
#define KEY_LEFT   0x100
#define KEY_RIGHT  0x101
#define KEY_UP     0x102
#define KEY_DOWN   0x103
#define KEY_HOME   0x104
#define KEY_END    0x105
#define KEY_DELETE 0x106
#define KEY_TAB    0x107
#define KEY_F4     0x108

void keyboard_init(void);
/* Inject a key from USB HID or other backends (ASCII or KEY_*). */
void keyboard_inject(int key);
void keyboard_set_modifiers(int shift, int ctrl, int alt);
int  keyboard_has_char(void);
char keyboard_getchar(void);
/* Non-blocking ASCII only: returns 0 if empty or next is special */
char keyboard_try_getchar(void);
/* Non-blocking: 0 empty, 1..127 ASCII, KEY_* special */
int  keyboard_try_getkey(void);
int  keyboard_ctrl_down(void);
int  keyboard_shift_down(void);
int  keyboard_alt_down(void);

#endif
