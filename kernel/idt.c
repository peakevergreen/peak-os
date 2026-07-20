#include "idt.h"
#include "pic.h"
#include "serial.h"
#include "util.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

static struct idt_entry idt[256];
static irq_handler_t irq_handlers[16];

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void idt_set(uint8_t vec, void *isr, uint8_t flags) {
    uint64_t addr = (uint64_t)isr;
    idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector    = 0x28; /* Limine 64-bit code segment */
    idt[vec].ist         = 0;
    idt[vec].type_attr   = flags;
    idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].zero        = 0;
}

void irq_install(uint8_t irq, irq_handler_t handler) {
    if (irq < 16)
        irq_handlers[irq] = handler;
}

void isr_dispatch(struct interrupt_frame *frame) {
    if (frame->int_no >= 32 && frame->int_no <= 47) {
        uint8_t irq = (uint8_t)(frame->int_no - 32);
        if (irq_handlers[irq])
            irq_handlers[irq](frame);
        pic_eoi(irq);
        return;
    }
    /* CPU exceptions — log and halt for MVP */
    serial_write_str("Exception: ");
    char buf[16];
    itoa_u(frame->int_no, buf, 10);
    serial_write_str(buf);
    serial_write_str("\n");
    for (;;)
        hlt();
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));

    idt_set(0,  isr0,  0x8E); idt_set(1,  isr1,  0x8E);
    idt_set(2,  isr2,  0x8E); idt_set(3,  isr3,  0x8E);
    idt_set(4,  isr4,  0x8E); idt_set(5,  isr5,  0x8E);
    idt_set(6,  isr6,  0x8E); idt_set(7,  isr7,  0x8E);
    idt_set(8,  isr8,  0x8E); idt_set(9,  isr9,  0x8E);
    idt_set(10, isr10, 0x8E); idt_set(11, isr11, 0x8E);
    idt_set(12, isr12, 0x8E); idt_set(13, isr13, 0x8E);
    idt_set(14, isr14, 0x8E); idt_set(15, isr15, 0x8E);
    idt_set(16, isr16, 0x8E); idt_set(17, isr17, 0x8E);
    idt_set(18, isr18, 0x8E); idt_set(19, isr19, 0x8E);
    idt_set(20, isr20, 0x8E); idt_set(21, isr21, 0x8E);
    idt_set(22, isr22, 0x8E); idt_set(23, isr23, 0x8E);
    idt_set(24, isr24, 0x8E); idt_set(25, isr25, 0x8E);
    idt_set(26, isr26, 0x8E); idt_set(27, isr27, 0x8E);
    idt_set(28, isr28, 0x8E); idt_set(29, isr29, 0x8E);
    idt_set(30, isr30, 0x8E); idt_set(31, isr31, 0x8E);

    idt_set(32, irq0,  0x8E); idt_set(33, irq1,  0x8E);
    idt_set(34, irq2,  0x8E); idt_set(35, irq3,  0x8E);
    idt_set(36, irq4,  0x8E); idt_set(37, irq5,  0x8E);
    idt_set(38, irq6,  0x8E); idt_set(39, irq7,  0x8E);
    idt_set(40, irq8,  0x8E); idt_set(41, irq9,  0x8E);
    idt_set(42, irq10, 0x8E); idt_set(43, irq11, 0x8E);
    idt_set(44, irq12, 0x8E); idt_set(45, irq13, 0x8E);
    idt_set(46, irq14, 0x8E); idt_set(47, irq15, 0x8E);

    lidt(idt, sizeof(idt) - 1);
}
