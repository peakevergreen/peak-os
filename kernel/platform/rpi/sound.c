#include "rpi.h"
#include "serial.h"

/* Audio ops are staged; PC-speaker semantics are currently a no-op on Pi. */

static int sound_ok;

void rpi_sound_init(void) {
    sound_ok = 0;
    serial_write_str("rpi: audio unavailable (PWM/I2S/HDMI beep not implemented)\n");
}

void rpi_sound_beep(uint32_t freq_hz, uint32_t ms) {
    (void)freq_hz;
    (void)ms;
    /* No-op until PWM/I2S/HDMI programming can produce sound. */
    if (!sound_ok)
        return;
}
