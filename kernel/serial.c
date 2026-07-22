#include "serial.h"
#include "arch.h"

void serial_init(void) {
    arch_serial_init();
}

void serial_write(char c) {
    if (c == '\n')
        arch_serial_write('\r');
    arch_serial_write(c);
}

void serial_write_str(const char *s) {
    while (*s)
        serial_write(*s++);
}
