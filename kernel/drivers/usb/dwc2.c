#include "usb.h"
#include "rpi.h"
#include "platform.h"
#include "arch.h"
#include "serial.h"
#include "util.h"
#include "timer.h"

/* Synopsys DWC2 host (BCM2837) — control + interrupt-IN for HID, with
 * single-TT hub enum, split transactions for FS/LS behind HS hubs, and
 * periodic port status polling for hotplug. */

extern void usb_hid_kbd_report(const uint8_t report[8]);
extern void usb_hid_mouse_report(const uint8_t *report, int len);
extern struct usb_device *usb_alloc_device(void);
extern void usb_register_hid_kbd(struct usb_device *d);
extern void usb_register_hid_mouse(struct usb_device *d);

#define DWC2_GAHBCFG   0x008
#define DWC2_GUSBCFG   0x00c
#define DWC2_GRSTCTL   0x010
#define DWC2_GINTSTS   0x014
#define DWC2_HCFG      0x400
#define DWC2_HFIR      0x404
#define DWC2_HPRT0     0x440
#define DWC2_HCCHAR(ch)   (0x500 + (ch) * 0x20)
#define DWC2_HCSPLT(ch)   (0x504 + (ch) * 0x20)
#define DWC2_HCINT(ch)    (0x508 + (ch) * 0x20)
#define DWC2_HCINTMSK(ch) (0x50c + (ch) * 0x20)
#define DWC2_HCTSIZ(ch)   (0x510 + (ch) * 0x20)
#define DWC2_HCDMA(ch)    (0x514 + (ch) * 0x20)

#define HCINT_XFERCOMP  (1u << 0)
#define HCINT_CHHLTD    (1u << 1)
#define HCINT_NAK       (1u << 4)
#define HCINT_ACK       (1u << 5)
#define HCINT_NYET      (1u << 6)

#define PID_DATA0 0
#define PID_DATA1 2
#define PID_SETUP 3

#define EP_CTRL 0
#define EP_INTR 3

#define USB_SPEED_HS 0
#define USB_SPEED_FS 1
#define USB_SPEED_LS 2

#define HUB_PORT_CONNECTION   0
#define HUB_PORT_ENABLE       1
#define HUB_PORT_RESET        4
#define HUB_PORT_POWER        8
#define HUB_C_PORT_CONNECTION 16
#define HUB_C_PORT_RESET      20

static volatile uint32_t *dwc;
static int dwc_ok;
static uint64_t last_poll;
static uint64_t last_hub_poll;
static uint8_t dma_buf[64] __attribute__((aligned(64)));
static uint8_t setup_pkt[8] __attribute__((aligned(64)));

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

struct hub_state {
    int used;
    uint8_t addr;
    uint8_t nports;
    uint8_t speed;
    uint8_t status_ep;
    uint16_t status_mps;
    uint8_t port_child[8]; /* child addr per port, 0 = empty */
    uint8_t port_seen[8];
};

#define MAX_HID 8
#define MAX_HUB 2
static struct hid_ep hid_eps[MAX_HID];
static int nhid;
static struct hub_state hubs[MAX_HUB];
static int nhub;
static uint8_t next_addr = 1;

static void dwc_write(uint32_t off, uint32_t v) { dwc[off / 4] = v; }
static uint32_t dwc_read(uint32_t off) { return dwc[off / 4]; }

static uint32_t virt_to_bus(void *p) {
    return (uint32_t)platform_virt_to_bus(p);
}

static void delay_spin(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++)
        ;
}

static void dwc_wait_ahb(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(dwc_read(DWC2_GRSTCTL) & (1u << 31)))
            return;
    }
}

static int dwc_reset_core(void) {
    dwc_wait_ahb();
    dwc_write(DWC2_GRSTCTL, 1u << 0);
    for (int i = 0; i < 1000000; i++) {
        if (!(dwc_read(DWC2_GRSTCTL) & 1u))
            break;
    }
    dwc_wait_ahb();
    return 0;
}

static int hc_wait(int ch) {
    for (int i = 0; i < 500000; i++) {
        uint32_t st = dwc_read(DWC2_HCINT(ch));
        if (st & HCINT_CHHLTD) {
            dwc_write(DWC2_HCINT(ch), st);
            if (st & HCINT_NAK)
                return 1;
            if (st & HCINT_NYET)
                return 2;
            if (st & HCINT_XFERCOMP)
                return 0;
            if (st & HCINT_ACK)
                return 0;
            return -1;
        }
    }
    return -1;
}

/* Optional split: hub_addr/port non-zero enables HCSPLT for FS/LS behind HS hub. */
static int hc_xfer(int ch, uint8_t addr, uint8_t ep, int in, int eptype,
                   uint8_t pid, void *buf, uint32_t len, uint16_t mps,
                   uint8_t speed, uint8_t hub_addr, uint8_t hub_port) {
    if (mps == 0)
        mps = 8;
    uint32_t pkts = len ? ((len + mps - 1) / mps) : 1;
    if (len == 0)
        pkts = 1;
    int do_split = (hub_addr != 0 && hub_port != 0 && speed != USB_SPEED_HS);
    platform_dma_clean(buf, len ? len : 1);
    if (in)
        platform_dma_invalidate(buf, len ? len : 1);

    if (!do_split) {
        dwc_write(DWC2_HCSPLT(ch), 0);
        dwc_write(DWC2_HCINT(ch), 0xffffffffu);
        dwc_write(DWC2_HCINTMSK(ch), 0);
        dwc_write(DWC2_HCTSIZ(ch), (pid << 29) | (pkts << 19) | len);
        dwc_write(DWC2_HCDMA(ch), virt_to_bus(buf));
        uint32_t charv = ((uint32_t)mps) | ((uint32_t)ep << 11) |
                         ((uint32_t)(in ? 1 : 0) << 15) | ((uint32_t)eptype << 18) |
                         ((uint32_t)addr << 22) | ((uint32_t)speed << 17) | (1u << 31);
        dwc_write(DWC2_HCCHAR(ch), charv);
        int r = hc_wait(ch);
        if (in && len)
            platform_dma_invalidate(buf, len);
        return r;
    }

    /* Start split */
    uint32_t splt = (1u << 31) | ((uint32_t)hub_addr) | ((uint32_t)hub_port << 7) |
                    (3u << 14); /* XACTPOS = all */
    dwc_write(DWC2_HCSPLT(ch), splt);
    dwc_write(DWC2_HCINT(ch), 0xffffffffu);
    dwc_write(DWC2_HCINTMSK(ch), 0);
    dwc_write(DWC2_HCTSIZ(ch), (pid << 29) | (pkts << 19) | len);
    dwc_write(DWC2_HCDMA(ch), virt_to_bus(buf));
    uint32_t charv = ((uint32_t)mps) | ((uint32_t)ep << 11) |
                     ((uint32_t)(in ? 1 : 0) << 15) | ((uint32_t)eptype << 18) |
                     ((uint32_t)addr << 22) | ((uint32_t)speed << 17) | (1u << 31);
    dwc_write(DWC2_HCCHAR(ch), charv);
    int r = hc_wait(ch);
    if (r < 0)
        return r;

    /* Complete split — retry on NYET */
    for (int attempt = 0; attempt < 8; attempt++) {
        delay_spin(2000);
        dwc_write(DWC2_HCSPLT(ch), splt | (1u << 16)); /* COMPSPLT */
        dwc_write(DWC2_HCINT(ch), 0xffffffffu);
        dwc_write(DWC2_HCTSIZ(ch), (pid << 29) | (pkts << 19) | len);
        dwc_write(DWC2_HCDMA(ch), virt_to_bus(buf));
        dwc_write(DWC2_HCCHAR(ch), charv);
        r = hc_wait(ch);
        if (r == 0) {
            if (in && len)
                platform_dma_invalidate(buf, len);
            return 0;
        }
        if (r == 1) /* NAK */
            return 1;
        /* NYET → retry complete split */
    }
    return -1;
}

static int ctrl_xfer_ex(uint8_t addr, uint8_t bmRequestType, uint8_t bRequest,
                        uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                        void *data, uint8_t speed, uint8_t hub_addr,
                        uint8_t hub_port, uint16_t mps) {
    setup_pkt[0] = bmRequestType;
    setup_pkt[1] = bRequest;
    setup_pkt[2] = (uint8_t)(wValue & 0xff);
    setup_pkt[3] = (uint8_t)(wValue >> 8);
    setup_pkt[4] = (uint8_t)(wIndex & 0xff);
    setup_pkt[5] = (uint8_t)(wIndex >> 8);
    setup_pkt[6] = (uint8_t)(wLength & 0xff);
    setup_pkt[7] = (uint8_t)(wLength >> 8);
    if (mps == 0)
        mps = (addr == 0) ? 8 : 64;
    if (hc_xfer(0, addr, 0, 0, EP_CTRL, PID_SETUP, setup_pkt, 8, mps, speed,
                hub_addr, hub_port) != 0)
        return -1;
    if (wLength) {
        int in = (bmRequestType & 0x80) != 0;
        uint8_t *dp = data ? (uint8_t *)data : dma_buf;
        if (hc_xfer(0, addr, 0, in, EP_CTRL, PID_DATA1, dp, wLength, mps, speed,
                    hub_addr, hub_port) != 0)
            return -1;
    }
    int status_in = (wLength == 0) || ((bmRequestType & 0x80) == 0);
    return hc_xfer(0, addr, 0, status_in, EP_CTRL, PID_DATA1, dma_buf, 0, mps,
                   speed, hub_addr, hub_port) == 0
               ? 0
               : -1;
}

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

static void clear_hid_for_addr(uint8_t addr) {
    for (int i = 0; i < nhid;) {
        if (hid_eps[i].addr == addr) {
            hid_eps[i] = hid_eps[nhid - 1];
            nhid--;
            continue;
        }
        i++;
    }
}

static void parse_config(uint8_t addr, uint8_t *cfg, int len, uint8_t speed,
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
                    (void)ctrl_xfer_ex(addr, 0x21, 0x0A, 0, iface_num, 0, 0,
                                       speed, hub_addr, hub_port, 8);
                    (void)ctrl_xfer_ex(addr, 0x21, 0x0B, 0, iface_num, 0, 0,
                                       speed, hub_addr, hub_port, 8);
                    add_hid(addr, ep & 0x0f, mps, 1, speed, hub_addr, hub_port);
                } else if (iface_proto == 2) {
                    (void)ctrl_xfer_ex(addr, 0x21, 0x0B, 0, iface_num, 0, 0,
                                       speed, hub_addr, hub_port, 8);
                    add_hid(addr, ep & 0x0f, mps, 0, speed, hub_addr, hub_port);
                }
            }
        }
        i += dlen;
    }
}

static int hub_set_port_feature(struct hub_state *h, uint8_t port,
                                uint16_t feature) {
    return ctrl_xfer_ex(h->addr, 0x23, 0x03, feature, port, 0, 0, h->speed, 0, 0,
                        64);
}

static int hub_clear_port_feature(struct hub_state *h, uint8_t port,
                                  uint16_t feature) {
    return ctrl_xfer_ex(h->addr, 0x23, 0x01, feature, port, 0, 0, h->speed, 0, 0,
                        64);
}

static int hub_get_port_status(struct hub_state *h, uint8_t port,
                               uint32_t *status) {
    uint8_t st[4];
    memset(st, 0, sizeof(st));
    if (ctrl_xfer_ex(h->addr, 0xA3, 0x00, 0, port, 4, st, h->speed, 0, 0, 64) !=
        0)
        return -1;
    *status = (uint32_t)st[0] | ((uint32_t)st[1] << 8) | ((uint32_t)st[2] << 16) |
              ((uint32_t)st[3] << 24);
    return 0;
}

static int enum_device_ex(uint8_t parent_hub, uint8_t parent_port, uint8_t speed,
                         uint8_t *out_addr);

static int register_hub(uint8_t addr, uint8_t speed, uint8_t *cfg, int cfg_len) {
    if (nhub >= MAX_HUB)
        return -1;
    uint8_t hubdesc[16];
    memset(hubdesc, 0, sizeof(hubdesc));
    if (ctrl_xfer_ex(addr, 0xA0, 0x06, 0x2900, 0, 9, hubdesc, speed, 0, 0, 64) !=
        0)
        return -1;
    uint8_t nports = hubdesc[2];
    if (nports == 0 || nports > 8)
        nports = hubdesc[2] ? hubdesc[2] : 4;
    if (nports > 8)
        nports = 8;

    struct hub_state *h = &hubs[nhub++];
    memset(h, 0, sizeof(*h));
    h->used = 1;
    h->addr = addr;
    h->nports = nports;
    h->speed = speed;

    /* Find hub status change interrupt EP */
    int i = 0;
    while (i + 2 <= cfg_len) {
        uint8_t dlen = cfg[i];
        uint8_t dtype = cfg[i + 1];
        if (dlen < 2 || i + dlen > cfg_len)
            break;
        if (dtype == 5 && dlen >= 7) {
            uint8_t ep = cfg[i + 2];
            uint8_t attr = cfg[i + 3];
            if ((ep & 0x80) && (attr & 3) == 3) {
                h->status_ep = ep & 0x0f;
                h->status_mps =
                    (uint16_t)cfg[i + 4] | ((uint16_t)cfg[i + 5] << 8);
                break;
            }
        }
        i += dlen;
    }

    char hub_msg[48];
    snprintf(hub_msg, sizeof(hub_msg), "usb: hub @%u ports=%u\n",
             (unsigned)addr, (unsigned)nports);
    serial_log(SERIAL_LOG_DEBUG, hub_msg);

    for (uint8_t p = 1; p <= nports; p++) {
        (void)hub_set_port_feature(h, p, HUB_PORT_POWER);
        delay_spin(100000);
        uint32_t st = 0;
        if (hub_get_port_status(h, p, &st) != 0)
            continue;
        if (st & (1u << HUB_PORT_CONNECTION)) {
            (void)hub_clear_port_feature(h, p, HUB_C_PORT_CONNECTION);
            (void)hub_set_port_feature(h, p, HUB_PORT_RESET);
            delay_spin(200000);
            (void)hub_clear_port_feature(h, p, HUB_C_PORT_RESET);
            delay_spin(50000);
            if (hub_get_port_status(h, p, &st) != 0)
                continue;
            uint8_t child_speed = USB_SPEED_FS;
            if (st & (1u << 10)) /* low speed */
                child_speed = USB_SPEED_LS;
            else if (st & (1u << 9)) /* high speed */
                child_speed = USB_SPEED_HS;
            uint8_t child = 0;
            if (enum_device_ex(addr, p, child_speed, &child) == 0) {
                h->port_child[p - 1] = child;
                h->port_seen[p - 1] = 1;
            }
        }
    }
    return 0;
}

static int enum_device_ex(uint8_t parent_hub, uint8_t parent_port, uint8_t speed,
                         uint8_t *out_addr) {
    uint8_t desc[18];
    memset(desc, 0, sizeof(desc));
    uint16_t mps0 = (speed == USB_SPEED_LS) ? 8 : 8;
    if (ctrl_xfer_ex(0, 0x80, 0x06, 0x0100, 0, 8, desc, speed, parent_hub,
                     parent_port, mps0) != 0)
        return -1;
    if (desc[7])
        mps0 = desc[7];
    uint8_t addr = next_addr++;
    if (next_addr == 0)
        next_addr = 1;
    if (ctrl_xfer_ex(0, 0x00, 0x05, addr, 0, 0, 0, speed, parent_hub, parent_port,
                     mps0) != 0)
        return -1;
    delay_spin(100000);
    if (ctrl_xfer_ex(addr, 0x80, 0x06, 0x0100, 0, 18, desc, speed, parent_hub,
                     parent_port, mps0) != 0)
        return -1;
    uint8_t cfg[256];
    memset(cfg, 0, sizeof(cfg));
    if (ctrl_xfer_ex(addr, 0x80, 0x06, 0x0200, 0, 9, cfg, speed, parent_hub,
                     parent_port, mps0) != 0)
        return -1;
    uint16_t total = (uint16_t)cfg[2] | ((uint16_t)cfg[3] << 8);
    if (total > sizeof(cfg))
        total = sizeof(cfg);
    if (ctrl_xfer_ex(addr, 0x80, 0x06, 0x0200, 0, total, cfg, speed, parent_hub,
                     parent_port, mps0) != 0)
        return -1;
    if (ctrl_xfer_ex(addr, 0x00, 0x09, cfg[5] ? cfg[5] : 1, 0, 0, 0, speed,
                     parent_hub, parent_port, mps0) != 0)
        return -1;

    int is_hub = (desc[4] == 0x09);
    if (!is_hub) {
        /* Interface-class hub */
        int i = 0;
        while (i + 9 <= (int)total) {
            if (cfg[i + 1] == 4 && cfg[i + 5] == 0x09) {
                is_hub = 1;
                break;
            }
            if (cfg[i] < 2)
                break;
            i += cfg[i];
        }
    }

    if (is_hub && parent_hub == 0) {
        (void)register_hub(addr, speed, cfg, (int)total);
    } else {
        parse_config(addr, cfg, (int)total, speed, parent_hub, parent_port);
    }
    if (out_addr)
        *out_addr = addr;
    return 0;
}

static void hub_poll_ports(void) {
    for (int hi = 0; hi < nhub; hi++) {
        struct hub_state *h = &hubs[hi];
        if (!h->used)
            continue;
        for (uint8_t p = 1; p <= h->nports; p++) {
            uint32_t st = 0;
            if (hub_get_port_status(h, p, &st) != 0)
                continue;
            int connected = (st & (1u << HUB_PORT_CONNECTION)) != 0;
            uint8_t child = h->port_child[p - 1];
            if (!connected && child) {
                clear_hid_for_addr(child);
                h->port_child[p - 1] = 0;
                h->port_seen[p - 1] = 0;
                serial_log_rl(SERIAL_LOG_DEBUG, 100,
                              "usb: hub port disconnect\n");
                (void)hub_clear_port_feature(h, p, HUB_C_PORT_CONNECTION);
            } else if (connected && !child) {
                (void)hub_clear_port_feature(h, p, HUB_C_PORT_CONNECTION);
                (void)hub_set_port_feature(h, p, HUB_PORT_RESET);
                delay_spin(200000);
                (void)hub_clear_port_feature(h, p, HUB_C_PORT_RESET);
                delay_spin(50000);
                if (hub_get_port_status(h, p, &st) != 0)
                    continue;
                uint8_t child_speed = USB_SPEED_FS;
                if (st & (1u << 10))
                    child_speed = USB_SPEED_LS;
                else if (st & (1u << 9))
                    child_speed = USB_SPEED_HS;
                uint8_t naddr = 0;
                if (enum_device_ex(h->addr, p, child_speed, &naddr) == 0) {
                    h->port_child[p - 1] = naddr;
                    h->port_seen[p - 1] = 1;
                    serial_log_rl(SERIAL_LOG_DEBUG, 100,
                                  "usb: hub port hotplug\n");
                }
            }
        }
    }
}

int dwc2_init(void) {
    uint64_t base = rpi_get()->usb_base;
    nhid = 0;
    nhub = 0;
    next_addr = 1;
    memset(hubs, 0, sizeof(hubs));
    if (!base) {
        serial_log(SERIAL_LOG_WARN, "dwc2: no base\n");
        return -1;
    }
    dwc = (volatile uint32_t *)(uintptr_t)base;
    dwc_reset_core();

    uint32_t usbcfg = dwc_read(DWC2_GUSBCFG);
    usbcfg &= ~(1u << 30);
    usbcfg |= (1u << 29);
    dwc_write(DWC2_GUSBCFG, usbcfg);
    dwc_write(DWC2_GAHBCFG, (1u << 5) | 1u);
    dwc_write(DWC2_HCFG, 0);
    dwc_write(DWC2_HFIR, 48000);

    uint32_t hprt = dwc_read(DWC2_HPRT0);
    hprt |= (1u << 12);
    dwc_write(DWC2_HPRT0, hprt);
    dwc_write(DWC2_GINTSTS, 0xffffffffu);
    dwc_ok = 1;
    serial_log(SERIAL_LOG_DEBUG, "dwc2: host init\n");

    for (int i = 0; i < 10; i++) {
        hprt = dwc_read(DWC2_HPRT0);
        if (hprt & 1u) {
            serial_log(SERIAL_LOG_DEBUG, "dwc2: device connected\n");
            hprt |= (1u << 8);
            dwc_write(DWC2_HPRT0, hprt);
            delay_spin(500000);
            hprt = dwc_read(DWC2_HPRT0);
            hprt &= ~(1u << 8);
            dwc_write(DWC2_HPRT0, hprt);
            delay_spin(100000);
            hprt = dwc_read(DWC2_HPRT0);
            dwc_write(DWC2_HPRT0, hprt | (1u << 1) | (1u << 3));
            /* Root port speed from HPRT0[17:18] */
            uint8_t spd = USB_SPEED_FS;
            uint32_t ps = (hprt >> 17) & 3u;
            if (ps == 0)
                spd = USB_SPEED_HS;
            else if (ps == 2)
                spd = USB_SPEED_LS;
            (void)enum_device_ex(0, 0, spd, 0);
            break;
        }
        delay_spin(200000);
    }
    return 0;
}

void dwc2_poll(void) {
    if (!dwc_ok)
        return;
    uint64_t now = timer_ticks();
    if (now - last_hub_poll >= 50) {
        last_hub_poll = now;
        hub_poll_ports();
    }
    if (nhid == 0)
        return;
    if (now - last_poll < 1)
        return;
    last_poll = now;
    for (int i = 0; i < nhid; i++) {
        struct hid_ep *h = &hid_eps[i];
        memset(dma_buf, 0, sizeof(dma_buf));
        int r = hc_xfer(1 + (i & 3), h->addr, h->ep, 1, EP_INTR, h->data_pid,
                        dma_buf, h->mps > 8 ? 8 : h->mps, h->mps, h->speed,
                        h->hub_addr, h->hub_port);
        if (r != 0)
            continue;
        h->data_pid = (h->data_pid == PID_DATA0) ? PID_DATA1 : PID_DATA0;
        if (h->is_kbd) {
            int changed = 0;
            for (int k = 0; k < 8; k++)
                if (dma_buf[k] != h->prev_kbd[k])
                    changed = 1;
            if (changed) {
                memcpy(h->prev_kbd, dma_buf, 8);
                usb_hid_kbd_report(dma_buf);
            }
        } else if (h->is_mouse) {
            if (dma_buf[1] || dma_buf[2] || (dma_buf[0] & 7))
                usb_hid_mouse_report(dma_buf, h->mps > 3 ? 4 : 3);
        }
    }
}
