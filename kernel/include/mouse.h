#ifndef PEAK_MOUSE_H
#define PEAK_MOUSE_H

#include "types.h"

struct mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons; /* bit0=left bit1=right bit2=middle */
    uint8_t left_pressed;
    uint8_t left_released;
    uint8_t right_pressed;
    uint8_t right_released;
    int8_t  wheel; /* +up / -down accumulated since last poll */
};

void mouse_init(void);
/* Inject relative motion / buttons from USB HID. */
void mouse_inject(int32_t dx, int32_t dy, uint8_t buttons, int8_t wheel);
void mouse_poll(struct mouse_state *out);
void mouse_clear_clicks(void);
int  mouse_buttons_any(void);

#endif
