#ifndef PEAK_SOUND_H
#define PEAK_SOUND_H

#include "types.h"

void sound_init(void);
/* Queue a PC-speaker beep (non-blocking). Call sound_poll from the UI loop. */
void sound_beep(uint32_t freq_hz, uint32_t duration_ms);
void sound_poll(void);
void sound_ui_click(void);
void sound_ui_notify(void);

#endif
