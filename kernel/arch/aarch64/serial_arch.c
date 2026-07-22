#include "arch.h"

/* Filled by platform before serial_init, or default PL011. */
extern void platform_uart_init(void);
extern void platform_uart_putc(char c);

void arch_serial_init(void) {
    platform_uart_init();
}

void arch_serial_write(char c) {
    platform_uart_putc(c);
}
