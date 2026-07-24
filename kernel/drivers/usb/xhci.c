#include "serial.h"
#include "timer.h"

/* Staged xHCI host for Pi 4 (VL805) and Pi 5 (RP1).
 * Stub only: command/event rings and transfer TRBs are not implemented.
 * Never marks the host ready; does not poke MMIO. */

static int xhci_ok;
static uint64_t last_poll;

int xhci_init(uint64_t mmio_base) {
    (void)mmio_base;
    xhci_ok = 0;
    serial_write_str("xhci: stub (not ready; rings/TRBs deferred)\n");
    return -1;
}

void xhci_poll(void) {
    if (!xhci_ok)
        return;
    uint64_t now = timer_ticks();
    if (now - last_poll < 2)
        return;
    last_poll = now;
    /* Event ring dequeue → HID reports once TRBs land. */
}
