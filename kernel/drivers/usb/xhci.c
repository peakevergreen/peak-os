#include "usb.h"
#include "serial.h"
#include "util.h"
#include "timer.h"

/* Staged xHCI host for Pi 4 (VL805) and Pi 5 (RP1). Controller reset is
 * diagnostic only; the host is unavailable until command/event rings and
 * transfer TRBs are implemented. */

extern void usb_hid_kbd_report(const uint8_t report[8]);
extern void usb_hid_mouse_report(const uint8_t *report, int len);
extern struct usb_device *usb_alloc_device(void);
extern void usb_register_hid_kbd(struct usb_device *d);
extern void usb_register_hid_mouse(struct usb_device *d);

static volatile uint8_t *cap;
static volatile uint32_t *op;
static int xhci_ok;
static uint64_t last_poll;

int xhci_init(uint64_t mmio_base) {
    if (!mmio_base) {
        serial_write_str("xhci: no mmio\n");
        return -1;
    }
    cap = (volatile uint8_t *)(uintptr_t)mmio_base;
    uint8_t caplength = cap[0];
    uint16_t hciver = (uint16_t)cap[2] | ((uint16_t)cap[3] << 8);
    op = (volatile uint32_t *)(uintptr_t)(mmio_base + caplength);

    serial_write_str("xhci: CAPLENGTH=");
    char b[12];
    itoa_u(caplength, b, 10);
    serial_write_str(b);
    serial_write_str(" HCI=");
    itoa_u(hciver, b, 16);
    serial_write_str(b);
    serial_write_str("\n");

    /* Halt if running, then HCRST */
    uint32_t usbcmd = op[0];
    if (usbcmd & 1u) {
        op[0] = usbcmd & ~1u;
        for (int i = 0; i < 100000; i++) {
            if (op[1] & 1u) /* HCH */
                break;
        }
    }
    op[0] = (1u << 1); /* HCRST */
    for (int i = 0; i < 1000000; i++) {
        if (!(op[0] & (1u << 1)))
            break;
    }

    xhci_ok = 0;
    serial_write_str("xhci: reset complete; unavailable (rings not implemented)\n");
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
