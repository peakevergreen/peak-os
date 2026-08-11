#include "usb.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "util.h"

#define USB_MAX_DEV 16

static struct usb_device devices[USB_MAX_DEV];
static int ndev;
static int usb_ready;
static uint32_t kbd_down[8];

static int hid_key_to_peak(uint8_t code) {
    static const char map[] = {
        0,0,0,0,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p',
        'q','r','s','t','u','v','w','x','y','z','1','2','3','4','5','6','7','8','9','0',
        '\n', 27, '\b', '\t', ' ', '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'
    };
    if (code < sizeof(map)) return (unsigned char)map[code];
    if (code == 0x4F) return KEY_RIGHT;
    if (code == 0x50) return KEY_LEFT;
    if (code == 0x51) return KEY_DOWN;
    if (code == 0x52) return KEY_UP;
    if (code == 0x4A) return KEY_HOME;
    if (code == 0x4D) return KEY_END;
    if (code == 0x4C) return KEY_DELETE;
    return 0;
}

static int kbd_bit(uint8_t code) {
    return (int)((kbd_down[code >> 5] >> (code & 31)) & 1u);
}

static void kbd_set_bit(uint8_t code, int on) {
    uint32_t mask = 1u << (code & 31);
    if (on) kbd_down[code >> 5] |= mask;
    else kbd_down[code >> 5] &= ~mask;
}

static void kbd_apply_peak(uint8_t code, int down) {
    int k = hid_key_to_peak(code);
    if (!k) return;
    if (keyboard_shift_down() && k >= 'a' && k <= 'z') k = k - 'a' + 'A';
    if (down) { keyboard_inject(k); keyboard_repeat_key_down(k); }
    else keyboard_repeat_key_up(k);
}

void usb_hid_kbd_report(const uint8_t report[8]) {
    /* Apply key ups before modifiers so Shift release + letter up still clear
     * software repeat via keyboard_repeat_key_up (any-up clears). */
    uint8_t present[256];
    memset(present, 0, sizeof(present));
    for (int i = 2; i < 8; i++)
        if (report[i]) present[report[i]] = 1;
    for (int code = 0; code < 256; code++) {
        int was = kbd_bit((uint8_t)code), now = present[code];
        if (!now && was) {
            kbd_set_bit((uint8_t)code, 0);
            kbd_apply_peak((uint8_t)code, 0);
        }
    }
    keyboard_set_modifiers((report[0] & 0x22) != 0, (report[0] & 0x11) != 0, (report[0] & 0x44) != 0);
    for (int code = 0; code < 256; code++) {
        int was = kbd_bit((uint8_t)code), now = present[code];
        if (now && !was) {
            kbd_set_bit((uint8_t)code, 1);
            kbd_apply_peak((uint8_t)code, 1);
        }
    }
}

void usb_hid_kbd_reset(void) {
    memset(kbd_down, 0, sizeof(kbd_down));
    keyboard_repeat_clear();
    keyboard_set_modifiers(0, 0, 0);
}

void usb_hid_mouse_report(const uint8_t *report, int len) {
    if (len < 3) return;
    mouse_inject((int8_t)report[1], (int8_t)report[2], report[0] & 7,
                 (len > 3) ? (int8_t)report[3] : 0);
}

int usb_device_count(void) { return ndev; }

struct usb_device *usb_alloc_device(void) {
    if (ndev >= USB_MAX_DEV) return 0;
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
    ndev = 0; usb_ready = 1;
    memset(kbd_down, 0, sizeof(kbd_down));
    serial_write_str("usb: core ready\n");
    return 0;
}

void usb_poll(void) { (void)usb_ready; }
