#include "pic.h"
#include "util.h"

#define PIC1        0x20
#define PIC2        0xA0
#define PIC1_CMD    PIC1
#define PIC1_DATA   (PIC1 + 1)
#define PIC2_CMD    PIC2
#define PIC2_DATA   (PIC2 + 1)
#define PIC_EOI     0x20

void pic_init(void) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20); /* IRQ 0-7 -> IDT 32-39 */
    io_wait();
    outb(PIC2_DATA, 0x28); /* IRQ 8-15 -> IDT 40-47 */
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);

    /* Mask all, then unmask as drivers init */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void pic_mask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  n = irq < 8 ? irq : (uint8_t)(irq - 8);
    outb(port, inb(port) | (uint8_t)(1u << n));
}

void pic_unmask(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  n = irq < 8 ? irq : (uint8_t)(irq - 8);
    outb(port, inb(port) & (uint8_t)~(1u << n));
}
