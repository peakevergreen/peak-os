#include "sound.h"

extern void rpi_sound_beep(uint32_t freq_hz, uint32_t ms);

void sound_init(void) {}

void sound_beep(uint32_t freq, uint32_t ms) {
    rpi_sound_beep(freq, ms);
}

void sound_poll(void) {}
void sound_ui_click(void) {}
void sound_ui_notify(void) {}
