#ifndef PEAK_IDT_H
#define PEAK_IDT_H

#include "types.h"

struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

typedef void (*irq_handler_t)(struct interrupt_frame *frame);

void idt_init(void);
void irq_install(uint8_t irq, irq_handler_t handler);

#endif
