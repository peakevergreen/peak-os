#include "serial.h"
#include "util.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* disable interrupts */
    outb(COM1 + 3, 0x80); /* DLAB on */
    outb(COM1 + 0, 0x03); /* 38400 baud divisor lo */
    outb(COM1 + 1, 0x00); /* divisor hi */
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* FIFO */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR */
}

static int serial_tx_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void serial_write(char c) {
    if (c == '\n')
        serial_write('\r');
    while (!serial_tx_empty())
        ;
    outb(COM1, (uint8_t)c);
}

void serial_write_str(const char *s) {
    while (*s)
        serial_write(*s++);
}
