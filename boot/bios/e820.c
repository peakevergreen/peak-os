#include "bios_call.h"
#include "boot_util.h"
#include "peak_boot.h"
#include <stdint.h>

/* Fixed low-memory scratch so ES:DI addressing is unambiguous. */
#define E820_BUF 0x1000u

uint32_t bios_e820(struct peak_mmap_entry *out, uint32_t max_entries) {
    uint32_t count = 0;
    uint32_t cont = 0;

    for (;;) {
        if (count >= max_entries)
            break;

        boot_memset((void *)(uintptr_t)E820_BUF, 0, 24);

        struct bios_regs r;
        boot_memset(&r, 0, sizeof(r));
        r.eax = 0xE820;
        r.ebx = cont;
        r.ecx = 24;
        r.edx = 0x534D4150; /* SMAP */
        r.edi = E820_BUF;
        r.es = 0;
        bios_int(0x15, &r);

        /* Success: CF clear and EAX = SMAP */
        if (r.flags & 1)
            break;
        if ((r.eax & 0xFFFFFFFFu) != 0x534D4150)
            break;

        uint8_t *buf = (uint8_t *)(uintptr_t)E820_BUF;
        uint64_t base = *(uint64_t *)buf;
        uint64_t len = *(uint64_t *)(buf + 8);
        uint32_t type = *(uint32_t *)(buf + 16);
        cont = r.ebx;

        if (len != 0) {
            uint32_t pt = PEAK_MMAP_RESERVED;
            if (type == 1)
                pt = PEAK_MMAP_USABLE;
            else if (type == 3)
                pt = PEAK_MMAP_ACPI_RECLAIM;
            else if (type == 4)
                pt = PEAK_MMAP_ACPI_NVS;
            else if (type == 5)
                pt = PEAK_MMAP_BAD;

            out[count].base = base;
            out[count].length = len;
            out[count].type = pt;
            out[count].reserved = 0;
            count++;
        }

        if (cont == 0)
            break;
    }
    return count;
}
