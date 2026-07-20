#ifndef PEAK_KEYBOARD_H
#define PEAK_KEYBOARD_H

#include "types.h"

void keyboard_init(void);
int  keyboard_has_char(void);
char keyboard_getchar(void);
/* Non-blocking: returns 0 if empty */
char keyboard_try_getchar(void);

#endif
