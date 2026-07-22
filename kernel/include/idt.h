#ifndef PEAK_IDT_H
#define PEAK_IDT_H

#include "types.h"
#include "irq.h"

#if defined(__x86_64__)
struct interrupt_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));
#else
/* aarch64 exception frame (subset used by SVC path). */
struct interrupt_frame {
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t esr;
};
#endif

void idt_init(void);

#endif
