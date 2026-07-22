#ifndef PEAK_BIOS_CALL_H
#define PEAK_BIOS_CALL_H

#include <stdint.h>

/* 32-bit register image for real-mode BIOS calls (low 16 bits used where needed). */
struct bios_regs {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp;
    uint16_t ds, es, flags;
    uint16_t _pad;
};

/* Invoke real-mode BIOS interrupt from 32-bit protected mode (no paging). */
void bios_int(uint8_t int_no, struct bios_regs *regs);

void bios_a20_enable(void);

#endif
