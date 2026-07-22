#include "fpu.h"
#include "util.h"

void fpu_init(void) {
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ull << 2);  /* clear EM (no x87 emulation) */
    cr0 |= (1ull << 1);   /* set MP */
    cr0 &= ~(1ull << 3);  /* clear TS */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9);   /* OSFXSR */
    cr4 |= (1ull << 10);  /* OSXMMEXCPT */
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4));

    /* Mask all MXCSR exceptions; round to nearest. */
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile ("ldmxcsr %0" : : "m"(mxcsr));
}

void fpu_save(void *fx512) {
    if (!fx512)
        return;
    __asm__ volatile ("fxsave (%0)" : : "r"(fx512) : "memory");
}

void fpu_restore(const void *fx512) {
    if (!fx512)
        return;
    __asm__ volatile ("fxrstor (%0)" : : "r"(fx512) : "memory");
}

void fpu_clear(void *fx512) {
    if (!fx512)
        return;
    memset(fx512, 0, 512);
    /* FCW = 0x037F, MXCSR = 0x1F80 defaults after FNINIT-ish layout */
    ((uint16_t *)fx512)[0] = 0x037F;
    ((uint32_t *)fx512)[6] = 0x1F80;
}
