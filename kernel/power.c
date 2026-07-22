#include "power.h"
#include "util.h"

void power_init(void) {
}

void power_shutdown(void) {
#if defined(__x86_64__)
    /* QEMU isa-debug-exit / ACPI shutdown ports */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
#else
    extern void platform_shutdown(void);
    platform_shutdown();
#endif
    for (;;)
        hlt();
}

void power_reboot(void) {
    reboot();
}
