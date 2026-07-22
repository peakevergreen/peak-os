#include "rpi.h"
#include "serial.h"

void rpi_gpu_init(void) {
    /* ASCII-only to avoid .rodata quirks with punctuation in early paths */
    serial_write_str("rpi: software framebuffer display; GPU acceleration unavailable\n");
}

int rpi_gpu_soft_fb(void) {
    return 1;
}

int rpi_gpu_accel_ready(void) {
    return 0;
}
