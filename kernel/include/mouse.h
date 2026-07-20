#ifndef PEAK_MOUSE_H
#define PEAK_MOUSE_H

#include "types.h"

struct mouse_state {
    int32_t x;
    int32_t y;
    uint8_t buttons; /* bit0=left bit1=right bit2=middle */
    uint8_t left_pressed;
    uint8_t left_released;
};

void mouse_init(void);
void mouse_poll(struct mouse_state *out);
void mouse_clear_clicks(void);

#endif
