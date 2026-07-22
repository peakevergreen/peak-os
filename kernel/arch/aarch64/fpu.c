#include "fpu.h"
#include "util.h"

void fpu_init(void) {
    uint64_t cpacr;
    __asm__ volatile ("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr |= (3ull << 20); /* FPEN = no trap */
    __asm__ volatile ("msr cpacr_el1, %0" : : "r"(cpacr));
    __asm__ volatile ("isb");
}

void fpu_save(void *buf) {
    if (!buf)
        return;
    __asm__ volatile (
        "stp q0, q1, [%0, #0]\n"
        "stp q2, q3, [%0, #32]\n"
        "stp q4, q5, [%0, #64]\n"
        "stp q6, q7, [%0, #96]\n"
        "stp q8, q9, [%0, #128]\n"
        "stp q10, q11, [%0, #160]\n"
        "stp q12, q13, [%0, #192]\n"
        "stp q14, q15, [%0, #224]\n"
        "stp q16, q17, [%0, #256]\n"
        "stp q18, q19, [%0, #288]\n"
        "stp q20, q21, [%0, #320]\n"
        "stp q22, q23, [%0, #352]\n"
        "stp q24, q25, [%0, #384]\n"
        "stp q26, q27, [%0, #416]\n"
        "stp q28, q29, [%0, #448]\n"
        "stp q30, q31, [%0, #480]\n"
        "mrs x9, fpsr\n"
        "str w9, [%0, #512]\n"
        "mrs x9, fpcr\n"
        "str w9, [%0, #516]\n"
        :
        : "r"(buf)
        : "x9", "memory"
    );
}

void fpu_restore(const void *buf) {
    if (!buf)
        return;
    /*
     * Deliberately omit vector clobbers: this routine changes the task's
     * architectural register state, including AAPCS callee-saved v8-v15.
     */
    __asm__ volatile (
        "ldp q0, q1, [%0, #0]\n"
        "ldp q2, q3, [%0, #32]\n"
        "ldp q4, q5, [%0, #64]\n"
        "ldp q6, q7, [%0, #96]\n"
        "ldp q8, q9, [%0, #128]\n"
        "ldp q10, q11, [%0, #160]\n"
        "ldp q12, q13, [%0, #192]\n"
        "ldp q14, q15, [%0, #224]\n"
        "ldp q16, q17, [%0, #256]\n"
        "ldp q18, q19, [%0, #288]\n"
        "ldp q20, q21, [%0, #320]\n"
        "ldp q22, q23, [%0, #352]\n"
        "ldp q24, q25, [%0, #384]\n"
        "ldp q26, q27, [%0, #416]\n"
        "ldp q28, q29, [%0, #448]\n"
        "ldp q30, q31, [%0, #480]\n"
        "ldr w9, [%0, #512]\n"
        "msr fpsr, x9\n"
        "ldr w9, [%0, #516]\n"
        "msr fpcr, x9\n"
        :
        : "r"(buf)
        : "x9", "memory"
    );
}

void fpu_clear(void *buf) {
    if (buf)
        memset(buf, 0, FPU_STATE_SIZE);
}
