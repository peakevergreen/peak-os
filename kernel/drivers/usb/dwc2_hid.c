#include "usb.h"
#include "dwc2_internal.h"
#include "util.h"

extern void usb_hid_kbd_report(const uint8_t report[8]);
extern void usb_hid_mouse_report(const uint8_t *report, int len);
extern struct usb_device *usb_alloc_device(void);
extern void usb_register_hid_kbd(struct usb_device *d);
extern void usb_register_hid_mouse(struct usb_device *d);

/* DWC2 HID interrupt-IN endpoints: config parse and polling. */

struct hid_ep {
    struct usb_device *dev;
    uint8_t addr;
    uint8_t ep;
    uint16_t mps;
    uint8_t speed;
    uint8_t hub_addr;
    uint8_t hub_port;
    int is_kbd;
    int is_mouse;
    uint8_t data_pid;
    uint8_t prev_kbd[8];
};

#define MAX_HID 8
static struct hid_ep hid_eps[MAX_HID];
static int nhid;

static void add_hid(uint8_t addr, uint8_t ep, uint16_t mps, int kbd,
                    uint8_t speed, uint8_t hub_addr, uint8_t hub_port) {
    if (nhid >= MAX_HID)
        return;
    struct usb_device *d = usb_alloc_device();
    if (!d)
        return;
    d->addr = addr;
    d->ep_in = ep;
    d->max_packet = mps;
    d->is_hid_kbd = kbd;
    d->is_hid_mouse = !kbd;
    if (kbd)
        usb_register_hid_kbd(d);
    else
        usb_register_hid_mouse(d);
    hid_eps[nhid].dev = d;
    hid_eps[nhid].addr = addr;
    hid_eps[nhid].ep = ep;
    hid_eps[nhid].mps = mps ? mps : 8;
    hid_eps[nhid].speed = speed;
    hid_eps[nhid].hub_addr = hub_addr;
    hid_eps[nhid].hub_port = hub_port;
    hid_eps[nhid].is_kbd = kbd;
    hid_eps[nhid].is_mouse = !kbd;
    hid_eps[nhid].data_pid = PID_DATA0;
    memset(hid_eps[nhid].prev_kbd, 0, 8);
    nhid++;
}

void dwc2_hid_clear_for_addr(uint8_t addr) {
    for (int i = 0; i < nhid;) {
        if (hid_eps[i].addr == addr) {
            hid_eps[i] = hid_eps[nhid - 1];
            nhid--;
            continue;
        }
        i++;
    }
}

void dwc2_hid_parse_config(uint8_t addr, uint8_t *cfg, int len, uint8_t speed,
                           uint8_t hub_addr, uint8_t hub_port) {
    int i = 0;
    uint8_t iface_class = 0, iface_sub = 0, iface_proto = 0, iface_num = 0;
    while (i + 2 <= len) {
        uint8_t dlen = cfg[i];
        uint8_t dtype = cfg[i + 1];
        if (dlen < 2 || i + dlen > len)
            break;
        if (dtype == 4 && dlen >= 9) {
            iface_num = cfg[i + 2];
            iface_class = cfg[i + 5];
            iface_sub = cfg[i + 6];
            iface_proto = cfg[i + 7];
        } else if (dtype == 5 && dlen >= 7) {
            uint8_t ep = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            uint16_t mps = (uint16_t)cfg[i + 4] | ((uint16_t)cfg[i + 5] << 8);
            if ((ep & 0x80) && (attr & 3) == 3 && iface_class == 3 &&
                iface_sub == 1) {
                if (iface_proto == 1) {
                    (void)dwc2_ctrl_xfer(addr, 0x21, 0x0A, 0, iface_num, 0, 0,
                                         speed, hub_addr, hub_port, 8);
                    (void)dwc2_ctrl_xfer(addr, 0x21, 0x0B, 0, iface_num, 0, 0,
                                         speed, hub_addr, hub_port, 8);
                    add_hid(addr, ep & 0x0f, mps, 1, speed, hub_addr, hub_port);
                } else if (iface_proto == 2) {
                    (void)dwc2_ctrl_xfer(addr, 0x21, 0x0B, 0, iface_num, 0, 0,
                                         speed, hub_addr, hub_port, 8);
                    add_hid(addr, ep & 0x0f, mps, 0, speed, hub_addr, hub_port);
                }
            }
        }
        i += dlen;
    }
}

void dwc2_hid_reset(void) {
    nhid = 0;
}

void dwc2_hid_poll(void) {
    if (nhid == 0)
        return;
    for (int i = 0; i < nhid; i++) {
        struct hid_ep *h = &hid_eps[i];
        memset(dwc2_dma_buf, 0, sizeof(dwc2_dma_buf));
        int r = dwc2_hc_xfer(1 + (i & 3), h->addr, h->ep, 1, EP_INTR, h->data_pid,
                             dwc2_dma_buf, h->mps > 8 ? 8 : h->mps, h->mps,
                             h->speed, h->hub_addr, h->hub_port);
        if (r != 0)
            continue;
        h->data_pid = (h->data_pid == PID_DATA0) ? PID_DATA1 : PID_DATA0;
        if (h->is_kbd) {
            int changed = 0;
            for (int k = 0; k < 8; k++)
                if (dwc2_dma_buf[k] != h->prev_kbd[k])
                    changed = 1;
            if (changed) {
                memcpy(h->prev_kbd, dwc2_dma_buf, 8);
                usb_hid_kbd_report(dwc2_dma_buf);
            }
        } else if (h->is_mouse) {
            if (dwc2_dma_buf[1] || dwc2_dma_buf[2] || (dwc2_dma_buf[0] & 7))
                usb_hid_mouse_report(dwc2_dma_buf, h->mps > 3 ? 4 : 3);
        }
    }
}
