#ifndef PEAK_KEYBOARD_H
#define PEAK_KEYBOARD_H
#include "types.h"
#define KEY_LEFT 0x100
#define KEY_RIGHT 0x101
#define KEY_UP 0x102
#define KEY_DOWN 0x103
#define KEY_HOME 0x104
#define KEY_END 0x105
#define KEY_DELETE 0x106
#define KEY_TAB 0x107
#define KEY_F4 0x108
void keyboard_init(void);
void keyboard_inject(int key);
void keyboard_set_modifiers(int shift, int ctrl, int alt);
void keyboard_poll(void);
void keyboard_set_repeat(uint32_t delay_ticks, uint32_t rate_ticks);
void keyboard_repeat_key_down(int key);
void keyboard_repeat_key_up(int key);
void keyboard_repeat_clear(void);
int keyboard_has_char(void);
char keyboard_getchar(void);
char keyboard_try_getchar(void);
int keyboard_try_getkey(void);
int keyboard_ctrl_down(void);
int keyboard_shift_down(void);
int keyboard_alt_down(void);
#endif
