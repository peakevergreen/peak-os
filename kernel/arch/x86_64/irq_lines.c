#include "irq.h"
#include "pic.h"

void irq_enable_line(uint32_t irq) {
    if (irq < 16)
        pic_unmask((uint8_t)irq);
}

void irq_disable_line(uint32_t irq) {
    if (irq < 16)
        pic_mask((uint8_t)irq);
}
