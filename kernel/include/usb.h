#ifndef PEAK_USB_H
#define PEAK_USB_H

#include "types.h"

struct usb_device {
    uint8_t addr;
    uint8_t class;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t ep_in;
    uint8_t ep_out;
    uint16_t max_packet;
    int is_hub;
    int is_hid_kbd;
    int is_hid_mouse;
};

int  usb_init(void);
void usb_poll(void);
int  usb_device_count(void);

#endif
