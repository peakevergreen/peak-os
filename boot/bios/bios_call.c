#include "bios_call.h"
#include "boot_util.h"

/* Must live in the loader image below 64 KiB for real-mode DS:offset access. */
static struct bios_regs low_regs;

void bios_int_asm(uint8_t int_no, struct bios_regs *regs);

void bios_int(uint8_t int_no, struct bios_regs *regs) {
    boot_memcpy(&low_regs, regs, sizeof(low_regs));
    bios_int_asm(int_no, &low_regs);
    boot_memcpy(regs, &low_regs, sizeof(low_regs));
}
