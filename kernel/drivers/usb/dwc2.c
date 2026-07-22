#include "usb.h"
#include "dwc2_internal.h"
#include "rpi.h"
#include "platform.h"
#include "arch.h"
#include "serial.h"
#include "util.h"
#include "timer.h"

/* Synopsys DWC2 host core (BCM2837): MMIO, channel xfers, init/poll entry. */

volatile uint32_t *dwc2_mmio;
int dwc2_ready;
uint8_t dwc2_dma_buf[64] __attribute__((aligned(64)));
uint8_t dwc2_setup_pkt[8] __attribute__((aligned(64)));
uint64_t dwc2_last_hub_poll;
uint8_t dwc2_next_addr = 1;

static uint64_t last_hid_poll;

uint32_t dwc2_read(uint32_t off) {
    return dwc2_mmio[off / 4];
}

void dwc2_write(uint32_t off, uint32_t v) {
    dwc2_mmio[off / 4] = v;
}

void dwc2_delay_spin(unsigned n) {
    for (volatile unsigned i = 0; i < n; i++)
        ;
}

static uint32_t virt_to_bus(void *p) {
    return (uint32_t)platform_virt_to_bus(p);
}

static void wait_ahb(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(dwc2_read(DWC2_GRSTCTL) & (1u << 31)))
            return;
    }
}

static int reset_core(void) {
    wait_ahb();
    dwc2_write(DWC2_GRSTCTL, 1u << 0);
    for (int i = 0; i < 1000000; i++) {
        if (!(dwc2_read(DWC2_GRSTCTL) & 1u))
            break;
    }
    wait_ahb();
    return 0;
}

static int hc_wait(int ch) {
    for (int i = 0; i < 500000; i++) {
        uint32_t st = dwc2_read(DWC2_HCINT(ch));
        if (st & HCINT_CHHLTD) {
            dwc2_write(DWC2_HCINT(ch), st);
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

int dwc2_hc_xfer(int ch, uint8_t addr, uint8_t ep, int in, int eptype,
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
        dwc2_write(DWC2_HCSPLT(ch), 0);
        dwc2_write(DWC2_HCINT(ch), 0xffffffffu);
        dwc2_write(DWC2_HCINTMSK(ch), 0);
        dwc2_write(DWC2_HCTSIZ(ch), (pid << 29) | (pkts << 19) | len);
        dwc2_write(DWC2_HCDMA(ch), virt_to_bus(buf));
        uint32_t charv = ((uint32_t)mps) | ((uint32_t)ep << 11) |
                         ((uint32_t)(in ? 1 : 0) << 15) | ((uint32_t)eptype << 18) |
                         ((uint32_t)addr << 22) | ((uint32_t)speed << 17) | (1u << 31);
        dwc2_write(DWC2_HCCHAR(ch), charv);
        int r = hc_wait(ch);
        if (in && len)
            platform_dma_invalidate(buf, len);
        return r;
    }

    uint32_t splt = (1u << 31) | ((uint32_t)hub_addr) | ((uint32_t)hub_port << 7) |
                    (3u << 14);
    dwc2_write(DWC2_HCSPLT(ch), splt);
    dwc2_write(DWC2_HCINT(ch), 0xffffffffu);
    dwc2_write(DWC2_HCINTMSK(ch), 0);
    dwc2_write(DWC2_HCTSIZ(ch), (pid << 29) | (pkts << 19) | len);
    dwc2_write(DWC2_HCDMA(ch), virt_to_bus(buf));
    uint32_t charv = ((uint32_t)mps) | ((uint32_t)ep << 11) |
                     ((uint32_t)(in ? 1 : 0) << 15) | ((uint32_t)eptype << 18) |
                     ((uint32_t)addr << 22) | ((uint32_t)speed << 17) | (1u << 31);
    dwc2_write(DWC2_HCCHAR(ch), charv);
    int r = hc_wait(ch);
    if (r < 0)
        return r;

    for (int attempt = 0; attempt < 8; attempt++) {
        dwc2_delay_spin(2000);
        dwc2_write(DWC2_HCSPLT(ch), splt | (1u << 16));
        dwc2_write(DWC2_HCINT(ch), 0xffffffffu);
        dwc2_write(DWC2_HCTSIZ(ch), (pid << 29) | (pkts << 19) | len);
        dwc2_write(DWC2_HCDMA(ch), virt_to_bus(buf));
        dwc2_write(DWC2_HCCHAR(ch), charv);
        r = hc_wait(ch);
        if (r == 0) {
            if (in && len)
                platform_dma_invalidate(buf, len);
            return 0;
        }
        if (r == 1)
            return 1;
    }
    return -1;
}

int dwc2_ctrl_xfer(uint8_t addr, uint8_t bmRequestType, uint8_t bRequest,
                   uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                   void *data, uint8_t speed, uint8_t hub_addr,
                   uint8_t hub_port, uint16_t mps) {
    dwc2_setup_pkt[0] = bmRequestType;
    dwc2_setup_pkt[1] = bRequest;
    dwc2_setup_pkt[2] = (uint8_t)(wValue & 0xff);
    dwc2_setup_pkt[3] = (uint8_t)(wValue >> 8);
    dwc2_setup_pkt[4] = (uint8_t)(wIndex & 0xff);
    dwc2_setup_pkt[5] = (uint8_t)(wIndex >> 8);
    dwc2_setup_pkt[6] = (uint8_t)(wLength & 0xff);
    dwc2_setup_pkt[7] = (uint8_t)(wLength >> 8);
    if (mps == 0)
        mps = (addr == 0) ? 8 : 64;
    if (dwc2_hc_xfer(0, addr, 0, 0, EP_CTRL, PID_SETUP, dwc2_setup_pkt, 8, mps,
                     speed, hub_addr, hub_port) != 0)
        return -1;
    if (wLength) {
        int in = (bmRequestType & 0x80) != 0;
        uint8_t *dp = data ? (uint8_t *)data : dwc2_dma_buf;
        if (dwc2_hc_xfer(0, addr, 0, in, EP_CTRL, PID_DATA1, dp, wLength, mps, speed,
                         hub_addr, hub_port) != 0)
            return -1;
    }
    int status_in = (wLength == 0) || ((bmRequestType & 0x80) == 0);
    return dwc2_hc_xfer(0, addr, 0, status_in, EP_CTRL, PID_DATA1, dwc2_dma_buf, 0,
                        mps, speed, hub_addr, hub_port) == 0
               ? 0
               : -1;
}

int dwc2_init(void) {
    uint64_t base = rpi_get()->usb_base;
    dwc2_hub_reset();
    dwc2_hid_reset();
    dwc2_next_addr = 1;
    if (!base) {
        serial_log(SERIAL_LOG_WARN, "dwc2: no base\n");
        return -1;
    }
    dwc2_mmio = (volatile uint32_t *)(uintptr_t)base;
    reset_core();

    uint32_t usbcfg = dwc2_read(DWC2_GUSBCFG);
    usbcfg &= ~(1u << 30);
    usbcfg |= (1u << 29);
    dwc2_write(DWC2_GUSBCFG, usbcfg);
    dwc2_write(DWC2_GAHBCFG, (1u << 5) | 1u);
    dwc2_write(DWC2_HCFG, 0);
    dwc2_write(DWC2_HFIR, 48000);

    uint32_t hprt = dwc2_read(DWC2_HPRT0);
    hprt |= (1u << 12);
    dwc2_write(DWC2_HPRT0, hprt);
    dwc2_write(DWC2_GINTSTS, 0xffffffffu);
    dwc2_ready = 1;
    serial_log(SERIAL_LOG_DEBUG, "dwc2: host init\n");

    for (int i = 0; i < 10; i++) {
        hprt = dwc2_read(DWC2_HPRT0);
        if (hprt & 1u) {
            serial_log(SERIAL_LOG_DEBUG, "dwc2: device connected\n");
            hprt |= (1u << 8);
            dwc2_write(DWC2_HPRT0, hprt);
            dwc2_delay_spin(500000);
            hprt = dwc2_read(DWC2_HPRT0);
            hprt &= ~(1u << 8);
            dwc2_write(DWC2_HPRT0, hprt);
            dwc2_delay_spin(100000);
            hprt = dwc2_read(DWC2_HPRT0);
            dwc2_write(DWC2_HPRT0, hprt | (1u << 1) | (1u << 3));
            uint8_t spd = USB_SPEED_FS;
            uint32_t ps = (hprt >> 17) & 3u;
            if (ps == 0)
                spd = USB_SPEED_HS;
            else if (ps == 2)
                spd = USB_SPEED_LS;
            (void)dwc2_hub_enum_device(0, 0, spd, 0);
            break;
        }
        dwc2_delay_spin(200000);
    }
    return 0;
}

void dwc2_poll(void) {
    if (!dwc2_ready)
        return;
    uint64_t now = timer_ticks();
    if (now - dwc2_last_hub_poll >= 50) {
        dwc2_last_hub_poll = now;
        dwc2_hub_poll_ports();
    }
    if (now - last_hid_poll < 1)
        return;
    last_hid_poll = now;
    dwc2_hid_poll();
}
