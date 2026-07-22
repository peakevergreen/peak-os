#include "usb.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "util.h"

#define USB_MAX_DEV 16

static struct usb_device devices[USB_MAX_DEV];
static int ndev;
static int usb_ready;

/* HID boot protocol keyboard usage → Peak key */
static int hid_key_to_peak(uint8_t code) {
    static const char map[] = {
        0,0,0,0,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p',
        'q','r','s','t','u','v','w','x','y','z','1','2','3','4','5','6','7','8','9','0',
        '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'
    };
    if (code < sizeof(map))
        return (unsigned char)map[code];
    if (code == 0x4F) return KEY_RIGHT;
    if (code == 0x50) return KEY_LEFT;
    if (code == 0x51) return KEY_DOWN;
    if (code == 0x52) return KEY_UP;
    if (code == 0x4A) return KEY_HOME;
    if (code == 0x4D) return KEY_END;
    if (code == 0x4C) return KEY_DELETE;
    return 0;
}

void usb_hid_kbd_report(const uint8_t report[8]) {
    int shift = (report[0] & 0x22) != 0;
    int ctrl = (report[0] & 0x11) != 0;
    int alt = (report[0] & 0x44) != 0;
    keyboard_set_modifiers(shift, ctrl, alt);
    for (int i = 2; i < 8; i++) {
        if (!report[i])
            continue;
        int k = hid_key_to_peak(report[i]);
        if (!k)
            continue;
        if (shift && k >= 'a' && k <= 'z')
            k = k - 'a' + 'A';
        keyboard_inject(k);
    }
}

void usb_hid_mouse_report(const uint8_t *report, int len) {
    if (len < 3)
        return;
    uint8_t buttons = report[0] & 7;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];
    int8_t wheel = (len > 3) ? (int8_t)report[3] : 0;
    mouse_inject(dx, dy, buttons, wheel);
}

int usb_device_count(void) {
    return ndev;
}

struct usb_device *usb_alloc_device(void) {
    if (ndev >= USB_MAX_DEV)
        return 0;
    struct usb_device *d = &devices[ndev++];
    memset(d, 0, sizeof(*d));
    return d;
}

void usb_register_hid_kbd(struct usb_device *d) {
    d->is_hid_kbd = 1;
    serial_write_str("usb: HID keyboard\n");
}

void usb_register_hid_mouse(struct usb_device *d) {
    d->is_hid_mouse = 1;
    serial_write_str("usb: HID mouse\n");
}

int usb_init(void) {
    ndev = 0;
    usb_ready = 1;
    serial_write_str("usb: core ready\n");
    return 0;
}

void usb_poll(void) {
    /* Host controller fills reports via IRQ/poll in dwc2/xhci. */
    (void)usb_ready;
}
