#include "random.h"
#include "serial.h"
#include "util.h"

#if defined(__GNUC__) || defined(__clang__)
uintptr_t __stack_chk_guard;

void __stack_chk_guard_setup(void) {
    uint8_t b[sizeof(uintptr_t)];
    if (random_get(RANDOM_DOMAIN_CANARY, b, sizeof(b)) != 0) {
        /* Non-crypto domain still available after random_init. */
        __stack_chk_guard = 0xDEADBEEFCAFEBABEULL ^ (uintptr_t)&b;
    } else {
        memcpy(&__stack_chk_guard, b, sizeof(__stack_chk_guard));
        memzero_explicit(b, sizeof(b));
    }
    /* Never all-zero. */
    if (__stack_chk_guard == 0)
        __stack_chk_guard = 0xA5A5A5A5A5A5A5A5ULL;
}

__attribute__((noreturn)) void __stack_chk_fail(void) {
    serial_write_str("FATAL: stack smashing detected\n");
    for (;;) {
#if defined(__x86_64__)
        __asm__ volatile ("cli; hlt" ::: "memory");
#else
        __asm__ volatile ("wfi" ::: "memory");
#endif
    }
}
#endif
